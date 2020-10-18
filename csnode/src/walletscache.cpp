#include <algorithm>
#include <map>

#include <blockchain.hpp>
#include <csdb/amount_commission.hpp>
#include <csnode/staking.hpp>
#include <csnode/walletscache.hpp>
#include <csnode/walletsids.hpp>
#include <lib/system/logger.hpp>
#include <solver/smartcontracts.hpp>

namespace {
const uint8_t kUntrustedMarker = 255;
const char* kLogPrefix = "WalletsCache: ";
}  // namespace

namespace cs {

WalletsCache::WalletsCache(WalletsIds& walletsIds) : walletsIds_(walletsIds) {}

std::unique_ptr<WalletsCache::Updater> WalletsCache::createUpdater() {
    return std::make_unique<Updater>(*this);
}

WalletsCache::Updater::Updater(WalletsCache& data)
  : data_(data),
    staking_(std::make_unique<Staking>(
      [this](const PublicKey& k) -> WalletsCache::WalletData& {
        return getWalletData(k);
      }
    )) {}

WalletsCache::Updater::~Updater() = default;

PublicKey WalletsCache::Updater::toPublicKey(const csdb::Address& addr) const {
    csdb::Address res;
    if (addr.is_public_key() || !data_.walletsIds_.normal().findaddr(addr.wallet_id(), res)) {
        return addr.public_key();
    }
    return res.public_key();
}

void WalletsCache::Updater::loadNextBlock(const csdb::Pool& pool,
                                          const cs::ConfidantsKeys& confidants,
                                          const BlockChain& blockchain,
                                          bool inverse /* = false */) {
    auto& transactions = pool.transactions();
    csdb::Amount totalAmountOfCountedFee = 0;

    for (auto itTrx = transactions.begin(); itTrx != transactions.end(); ++itTrx) {
        totalAmountOfCountedFee += load(*itTrx, blockchain, inverse);
        if (SmartContracts::is_new_state(*itTrx)) {
            fundConfidantsWalletsWithExecFee(*itTrx, blockchain, inverse);
        }
    }

    if (totalAmountOfCountedFee > csdb::Amount(0)) {
        fundConfidantsWalletsWithFee(totalAmountOfCountedFee, confidants,
                                     cs::Utils::bitsToMask(pool.numberTrusted(),
                                     pool.realTrusted()), inverse);
    }

    staking_->cleanObsoletteDelegations(BlockChain::getBlockTime(pool));

#ifdef MONITOR_NODE
    const auto& wrWall = pool.writer_public_key();

    auto it_writer = data_.trusted_info_.find(wrWall);
    if (it_writer == data_.trusted_info_.end()) {
        auto res = data_.trusted_info_.insert(std::make_pair(wrWall, TrustedData()));
        it_writer = res.first;
    }
    if (!inverse) ++it_writer->second.times;
    else --it_writer->second.times;

    for (const auto& it : confidants) {
        auto it_trusted = data_.trusted_info_.find(it);
        if (it_trusted == data_.trusted_info_.end()) {
            const auto res = data_.trusted_info_.insert(std::make_pair(it, TrustedData()));
            it_trusted = res.first;
        }
        if (!inverse) ++it_trusted->second.times_trusted;
        else --it_trusted->second.times_trusted;
    }
    auto timeStamp = atoll(pool.user_field(0).value<std::string>().c_str());
    setWalletTime(wrWall, timeStamp);
#endif
}

void WalletsCache::Updater::invokeReplenishPayableContract(const csdb::Transaction& transaction, bool inverse /* = false */) {
    auto& wallData = getWalletData(transaction.target());

    if (!inverse) {
        wallData.balance_ -= transaction.amount();
        data_.smartPayableTransactions_.push_back(transaction.id());
    }
    else {
        wallData.balance_ += transaction.amount();
        data_.smartPayableTransactions_.remove(transaction.id());
    }

    if (!SmartContracts::is_executable(transaction)) {
        auto& sourceWallData = getWalletData(transaction.source());
        if (!inverse) {
            sourceWallData.balance_ += csdb::Amount(transaction.counted_fee().to_double());
            sourceWallData.balance_ -= csdb::Amount(transaction.max_fee().to_double());
        }
        else {
            sourceWallData.balance_ -= csdb::Amount(transaction.counted_fee().to_double());
            sourceWallData.balance_ += csdb::Amount(transaction.max_fee().to_double());
        }

        emit walletUpdateEvent(toPublicKey(transaction.source()), sourceWallData);
    }

    emit walletUpdateEvent(toPublicKey(transaction.target()), wallData);
}

void WalletsCache::Updater::smartSourceTransactionReleased(const csdb::Transaction& smartSourceTrx,
                                                           const csdb::Transaction& initTrx,
                                                           bool inverse /* = false */) {
    auto countedFee = csdb::Amount(smartSourceTrx.counted_fee().to_double());

    auto& smartWallData = getWalletData(smartSourceTrx.source());
    auto& initWallData = getWalletData(initTrx.source());

    if (!inverse) {
        smartWallData.balance_ += countedFee;
        initWallData.balance_ -= countedFee;
    }
    else {
        smartWallData.balance_ -= countedFee;
        initWallData.balance_ += countedFee;
    }

    emit walletUpdateEvent(toPublicKey(smartSourceTrx.source()), smartWallData);
    emit walletUpdateEvent(toPublicKey(initTrx.source()), initWallData);
}

void WalletsCache::Updater::rollbackExceededTimeoutContract(const csdb::Transaction& transaction,
                                                            const csdb::Amount& execFee,
                                                            bool inverse /* = false */) {
    auto& wallData = getWalletData(transaction.source());
    if (!inverse) {
        wallData.balance_ += transaction.amount();
        wallData.balance_ += csdb::Amount(transaction.max_fee().to_double());
        wallData.balance_ -= csdb::Amount(transaction.counted_fee().to_double());
    }
    else {
        wallData.balance_ -= transaction.amount();
        wallData.balance_ -= csdb::Amount(transaction.max_fee().to_double());
        wallData.balance_ += csdb::Amount(transaction.counted_fee().to_double());
    }

    if (SmartContracts::is_executable(transaction)) {
        auto it = data_.canceledSmarts_.find(transaction.target());
        if (it == data_.canceledSmarts_.end() && !inverse) {
            data_.canceledSmarts_.insert(std::make_pair(transaction.target(), std::list<csdb::TransactionID>{transaction.id()}));
        }
        else {
            if (!inverse) {
                it->second.push_front(transaction.id());
            }
            else {
                it->second.remove(transaction.id());
                if (it->second.size() == 0) {
                    data_.canceledSmarts_.erase(transaction.target());
                }
            }
        }
    }
    else {
        auto it = std::find(data_.smartPayableTransactions_.cbegin(), data_.smartPayableTransactions_.cend(), transaction.id());
        if (it != data_.smartPayableTransactions_.cend() && !inverse) {
            data_.smartPayableTransactions_.erase(it);
            wallData.balance_ -= execFee;
        }
        else {
            if (inverse) {
                data_.smartPayableTransactions_.push_back(transaction.id());
                wallData.balance_ += execFee;
            }
        }
    }

    emit walletUpdateEvent(toPublicKey(transaction.source()), wallData);
}

#ifdef MONITOR_NODE
bool WalletsCache::Updater::setWalletTime(const PublicKey& address, const uint64_t& p_timeStamp) {
    auto it = data_.wallets_.find(address);
    if (it != data_.wallets_.end()) {
        if (it->second.createTime_ == 0) {
            it->second.createTime_ = p_timeStamp;
        }
        emit walletUpdateEvent(it->first, it->second);
        return true;
    }
    return false;
}
#endif

void WalletsCache::Updater::fundConfidantsWalletsWithFee(const csdb::Amount& totalFee,
                                                         const cs::ConfidantsKeys& confidants,
                                                         const std::vector<uint8_t>& realTrusted,
                                                         bool inverse) {
    if (!confidants.size()) {
        cslog() << kLogPrefix << "NO CONFIDANTS";
        return;
    }

    csdb::Amount totalStake = 0;
    std::map<PublicKey, csdb::Amount> confidantAndStake;

    for (size_t i = 0; i < confidants.size() && i < realTrusted.size(); ++i) {
        if (realTrusted[i] == kUntrustedMarker) {
            continue;
        }

        auto& wallet = getWalletData(confidants[i]);
        totalStake += wallet.balance_;
        confidantAndStake[confidants[i]] += wallet.balance_;

        auto miningDelegations = staking_->getMiningDelegations(confidants[i]);
        if (!miningDelegations) {
            continue;
        }

        for (auto& keyAndStake : *miningDelegations) {
            confidantAndStake[keyAndStake.first] += keyAndStake.second.amount;
            totalStake += keyAndStake.second.amount;
        }
    }

    csdb::Amount feeWithMining = totalFee * 2;
    csdb::Amount onePartOfFee = feeWithMining / totalStake;
    csdb::Amount payedFee = 0;
    size_t numPayedTrusted = 0;

    for (auto& confAndStake : confidantAndStake) {
            auto& walletData = getWalletData(confAndStake.first);
            csdb::Amount feeToPay = 0; 

            if (numPayedTrusted == confidantAndStake.size() - 1) {
                feeToPay = feeWithMining - payedFee;
            }
            else {
                feeToPay = onePartOfFee * confAndStake.second;
            }

            if (!inverse) {
                walletData.balance_ += feeToPay;
            }
            else {
                walletData.balance_ -= feeToPay;
            }

#ifdef MONITOR_NODE
            auto it_writer = data_.trusted_info_.find(confAndStake.first);
            if (it_writer != data_.trusted_info_.end()) {
                if (!inverse) {
                    it_writer->second.totalFee += feeToPay;
                }
                else {
                    it_writer->second.totalFee -= feeToPay;
                }
            }
#endif

            payedFee += feeToPay;
            ++numPayedTrusted;

            emit walletUpdateEvent(confAndStake.first, walletData);
    }
}

void WalletsCache::Updater::fundConfidantsWalletsWithExecFee(const csdb::Transaction& transaction,
                                                             const BlockChain& blockchain,
                                                             bool inverse) {
    if (!SmartContracts::is_new_state(transaction)) {
        csmeta(cswarning) << "transaction is not new state";
        return;
    }

    SmartContractRef smartRef(transaction.user_field(trx_uf::new_state::RefStart));
    if (!smartRef.is_valid()) {
        csmeta(cserror) << "incorrect reference to starter transaction in new state";
        return;
    }

    csdb::Pool pool = blockchain.loadBlock(smartRef.sequence);
    if (!pool.is_valid()) {
        csmeta(cserror) << "invalid pool";
        return;
    }

    fundConfidantsWalletsWithFee(
        transaction.user_field(trx_uf::new_state::Fee).value<csdb::Amount>(),
        pool.confidants(),
        cs::Utils::bitsToMask(pool.numberTrusted(), pool.realTrusted()),
        inverse
    );
}

double WalletsCache::Updater::loadTrxForSource(const csdb::Transaction& tr,
                                               const BlockChain& blockchain,
                                               bool inverse) {
    csdb::Address wallAddress;
    bool smartIniter = false;
    csdb::Transaction initTransaction;
    if (SmartContracts::is_new_state(tr)) {
        csdb::UserField fld = tr.user_field(cs::trx_uf::new_state::RefStart);
        if (fld.is_valid()) {
            cs::SmartContractRef ref(fld);
            csdb::Pool pool = blockchain.loadBlock(ref.sequence);
            if (pool.is_valid() && pool.transactions_count() > ref.transaction) {
                initTransaction = pool.transactions()[ref.transaction];
                wallAddress = initTransaction.source();
            }
        }
        smartIniter = true;
    }
    else {
        wallAddress = tr.source();
    }

    // Attention!!!
    // In case of contract new state (smartIniter == true) and wallData is initer, not contract
    // Otherwise (smartIniter = false) and wallData is tr.source()

    auto& wallData = getWalletData(wallAddress);

    if (SmartContracts::is_executable(tr)) {
        if (!inverse) {
            wallData.balance_ -= csdb::Amount(tr.max_fee().to_double());
        }
        else {
            wallData.balance_ += csdb::Amount(tr.max_fee().to_double());
        }
    }
    else if (SmartContracts::is_new_state(tr)) {
        if (!initTransaction.is_valid()) {
            const auto id = tr.id();
            csdebug() << kLogPrefix << "failed to load init transaction for new state " << FormatRef(id.pool_seq(), (size_t)id.index());
        }
        else {
            const auto start_id = initTransaction.id();
            const auto start_target = initTransaction.target();
            const csdb::Amount start_max_fee(initTransaction.max_fee().to_double());
            const csdb::Amount start_counted_fee(initTransaction.counted_fee().to_double());
            if (isCanceledSmart(start_target, start_id)) {
                csdebug() << kLogPrefix << "(deprecated behaviour) contract "
                    << FormatRef(start_id.pool_seq(), (size_t )start_id.index())
                    << " was canceled before , and now new_state is found in "
                    << WithDelimiters(tr.id().pool_seq());
                if (!inverse) {
                    wallData.balance_ -= initTransaction.amount();
                    wallData.balance_ -= start_max_fee;
                    wallData.balance_ += start_counted_fee;
                }
                else {
                    wallData.balance_ += initTransaction.amount();
                    wallData.balance_ += start_max_fee;
                    wallData.balance_ -= start_counted_fee;
                }
            }
            checkCanceledSmart(start_target, start_id, inverse);
            if (SmartContracts::is_executable(initTransaction)) {
                if (!inverse) {
                    wallData.balance_ += start_max_fee;
                    wallData.balance_ -= start_counted_fee;
                    wallData.balance_ -= csdb::Amount(tr.counted_fee().to_double());
                    wallData.balance_ -= csdb::Amount(tr.user_field(trx_uf::new_state::Fee).value<csdb::Amount>());
                }
                else {
                    wallData.balance_ -= start_max_fee;
                    wallData.balance_ += start_counted_fee;
                    wallData.balance_ += csdb::Amount(tr.counted_fee().to_double());
                    wallData.balance_ += csdb::Amount(tr.user_field(trx_uf::new_state::Fee).value<csdb::Amount>());
                }
            }
            else {
                checkSmartWaitingForMoney(initTransaction, tr, inverse);
            }
        }
        auto& wallData_s = getWalletData(tr.source());
        if (!inverse) {
            ++wallData_s.transNum_;
            wallData_s.trxTail_.push(tr.innerID());
            wallData_s.lastTransaction_ = tr.id();

            auto pubKey = toPublicKey(tr.source());
            csdetails() << "Wallets: innerID of (new_state) "
                        << EncodeBase58(cs::Bytes(pubKey.begin(), pubKey.end()))
                        << " <- " << tr.innerID();
        }
        else {
            --wallData_s.transNum_;
        }

        emit walletUpdateEvent(toPublicKey(tr.source()), wallData_s);
    }
    else {
        if (!inverse) {
            wallData.balance_ -= csdb::Amount(tr.counted_fee().to_double());
        }
        else {
            wallData.balance_ += csdb::Amount(tr.counted_fee().to_double());
        }
    }
	//wallData = sources Account
    if (!smartIniter) {
        csdb::UserField ufld = tr.user_field(trx_uf::sp::delegated);
        if (!inverse) {
            if (ufld.is_valid()) {
                auto tKey = toPublicKey(tr.target());
                if (wallData.delegateTargets_ == nullptr) {
                    wallData.delegateTargets_ = std::make_shared<std::map<cs::PublicKey, std::vector<cs::TimeMoney>>>();
                }
                auto it = wallData.delegateTargets_->find(tKey);
                if (ufld.value<uint64_t>() == trx_uf::sp::de::legate) {
                    wallData.balance_ -= tr.amount();
                    cs::TimeMoney tm(cs::Zero::timeStamp, tr.amount());
                    if (it == wallData.delegateTargets_->end()) {
                        std::vector<cs::TimeMoney> firstElement;
                        firstElement.push_back(tm);
                        wallData.delegateTargets_->emplace(tKey, firstElement);
                    }
                    else {
                        auto itt = std::find_if(it->second.begin(), it->second.end(), [](cs::TimeMoney& tm) {return tm.time == cs::Zero::timeStamp; });
                        if (itt == it->second.end()) {
                            it->second.push_back(tm);
                        }
                        else {
                            itt->amount += tr.amount();
                        }
                    }
                }
                else if (ufld.value<uint64_t>() == trx_uf::sp::de::legated_withdraw) {
                    if (it != wallData.delegateTargets_->end()) {
                        auto itt = std::find_if(it->second.begin(), it->second.end(), [](cs::TimeMoney& tm) {return tm.time == cs::Zero::timeStamp; });
                        if (itt != it->second.end()) {
                            itt->amount -= tr.amount();
                            wallData.balance_ += tr.amount();
                            //removing empty records
                            if (itt->amount == csdb::Amount{ 0 }) {
                                it->second.erase(itt);
                                if (it->second.size() == 0U) {
                                    wallData.delegateTargets_->erase(tKey);
                                }
                            }
                        }
                    }
                }
                else if (ufld.value<uint64_t>() >= trx_uf::sp::de::legate_min_utc) {
                    cs::TimeMoney tm(ufld.value<uint64_t>() , tr.amount());
                    wallData.balance_ -= tr.amount();
                    if (it == wallData.delegateTargets_->end()) {
                        std::vector<cs::TimeMoney> firstElement;
                        firstElement.push_back(tm);
                        wallData.delegateTargets_->emplace(tKey, firstElement);
                    }
                    else {
                        it->second.push_back(tm);
                    }
                }
                else {
                    cserror() << "WalletCache: no such delegats in sources list 1";
                }
            }
            else {
                wallData.balance_ -= tr.amount();
            }
            ++wallData.transNum_;
            wallData.trxTail_.push(tr.innerID());
            wallData.lastTransaction_ = tr.id();

            auto pubKey = toPublicKey(wallAddress);
            csdetails() << "Wallets: innerID of "
                        << EncodeBase58(cs::Bytes(pubKey.begin(), pubKey.end()))
                        << " <- " << tr.innerID();

#ifdef MONITOR_NODE        
            setWalletTime(pubKey, tr.get_time());
#endif
        }
        else {
            auto tKey = toPublicKey(tr.target());
            if (wallData.delegateTargets_ == nullptr) {
                wallData.delegateTargets_ = std::make_shared<std::map<cs::PublicKey, std::vector<cs::TimeMoney>>>();
            }
            if (ufld.is_valid()) {
                //delegate transaction(inverse)
                auto it = wallData.delegateTargets_->find(tKey);
                if (ufld.value<uint64_t>() == trx_uf::sp::de::legate) {
                    if (it != wallData.delegateTargets_->end()) {
                        auto itt = std::find_if(it->second.begin(), it->second.end(), [](cs::TimeMoney& tm) {return tm.time == cs::Zero::timeStamp; });
                        if (itt != it->second.end()) {
                            itt->amount -= tr.amount();
                            wallData.balance_ += tr.amount();
                            //removing empty records
                            if (itt->amount == csdb::Amount{ 0 }) {
                                it->second.erase(itt);
                                if (it->second.size() == 0U) {
                                    wallData.delegateTargets_->erase(tKey);
                                }
                            }
                        }
                    }
                }

                //withdraw delegation (inverse)
                else if (ufld.value<uint64_t>() == trx_uf::sp::de::legated_withdraw) {
                    wallData.balance_ -= tr.amount();
                    cs::TimeMoney tm(cs::Zero::timeStamp, tr.amount());
                    if (it == wallData.delegateTargets_->end()) {
                        std::vector<cs::TimeMoney> firstElement;
                        firstElement.push_back(tm);
                        wallData.delegateTargets_->emplace(tKey, firstElement);
                    }
                    else {
                        auto itt = std::find_if(it->second.begin(), it->second.end(), [](cs::TimeMoney& tm) {return tm.time == cs::Zero::timeStamp; });
                        if (itt == it->second.end()) {
                            it->second.push_back(tm);
                        }
                        else {
                            itt->amount += tr.amount();
                        }
                    }
                }

                else if (ufld.value<uint64_t>() >= trx_uf::sp::de::legate_min_utc) {
                    //nothing to do if time of this transacton is already finished
                    auto sKey = toPublicKey(tr.source());
                    staking_->removeSingleDelegation(ufld.value<uint64_t>(), sKey, tKey, tr.id());
                }
                else {
                    cserror() << "WalletCache: error as source in delegations 2";
                }

            }
            else {
                wallData.balance_ += tr.amount();
            }

            --wallData.transNum_;
        }
    }

    if (inverse) {
        if (smartIniter) {
            wallAddress = tr.source();
        }
        auto& wallData_s = getWalletData(wallAddress);
        auto pubKey = toPublicKey(wallAddress);
        csdetails() << "Wallets: erase innerID of "
            << EncodeBase58(cs::Bytes(pubKey.begin(), pubKey.end()))
            << " -> " << tr.innerID();
        wallData_s.trxTail_.erase(tr.innerID());
    }

    emit walletUpdateEvent(toPublicKey(wallAddress), wallData);
    return tr.counted_fee().to_double();
}

bool WalletsCache::Updater::isCanceledSmart(const csdb::Address& contract_addr, const csdb::TransactionID& tid) {
    auto it = data_.canceledSmarts_.find(contract_addr);
    if (it == data_.canceledSmarts_.end()) {
        return false;
    }
    const auto seq = tid.pool_seq();
    const auto idx = tid.index();
    return it->second.end() != std::find_if(it->second.begin(), it->second.end(),
        [&](const csdb::TransactionID& item) { return item.pool_seq() == seq && item.index() == idx; });
}

void WalletsCache::Updater::checkCanceledSmart(const csdb::Address& contract_addr,
    const csdb::TransactionID& tid,
    bool /*inverse*/) {

   /* if (inverse) {
        auto it = data_.canceledSmarts_.find(contract_addr);
        if (it == data_.canceledSmarts_.end()) {
            data_.canceledSmarts_.insert(std::make_pair(contract_addr, std::list<csdb::TransactionID>{tid}));
        }
        else {
            it->second.push_front(tid);
        }
        return;
    }*/

    auto it = data_.canceledSmarts_.find(contract_addr);
    if (it == data_.canceledSmarts_.end()) {
        return;
    }
    const auto seq = tid.pool_seq();
    const auto idx = tid.index();
    for (auto i = it->second.cbegin(); i != it->second.cend(); ++i) {
        const auto s = i->pool_seq();
        if (s <= seq || (s == seq && i->index() <= idx)) {
            it->second.erase(i, it->second.cend());
            break;
        }
    }
    if (it->second.empty()) {
        data_.canceledSmarts_.erase(it);
    }
}

void WalletsCache::Updater::checkSmartWaitingForMoney(const csdb::Transaction& initTransaction,
    const csdb::Transaction& newStateTransaction,
    bool inverse) {
    if (!cs::SmartContracts::is_state_updated(newStateTransaction)) {
        csdb::Amount fee(0);
        csdb::UserField fld = newStateTransaction.user_field(trx_uf::new_state::Fee);
        if (fld.is_valid()) {
            fee = fld.value<csdb::Amount>();
        }
        rollbackExceededTimeoutContract(initTransaction, fee, inverse);
        auto& wallDataIniter = getWalletData(initTransaction.source());
        if (!inverse) {
            wallDataIniter.balance_ -= csdb::Amount(newStateTransaction.counted_fee().to_double());
        }
        else {
            wallDataIniter.balance_ += csdb::Amount(newStateTransaction.counted_fee().to_double());
        }
        return;
    }
    bool waitingSmart = false;
    auto it = std::find(data_.smartPayableTransactions_.cbegin(), data_.smartPayableTransactions_.cend(), initTransaction.id());
    if (it != data_.smartPayableTransactions_.cend()) {
        if (!inverse) {
            data_.smartPayableTransactions_.erase(it);
            waitingSmart = true;
        }
    }
    else {
        if (inverse) {
            data_.smartPayableTransactions_.push_back(initTransaction.id());

            auto& wallDataIniter = getWalletData(initTransaction.source());
            wallDataIniter.balance_ += csdb::Amount(newStateTransaction.user_field(trx_uf::new_state::Fee).value<csdb::Amount>());
            wallDataIniter.balance_ += csdb::Amount(initTransaction.counted_fee().to_double());
            wallDataIniter.balance_ -= csdb::Amount(initTransaction.max_fee().to_double());
            wallDataIniter.balance_ += csdb::Amount(newStateTransaction.counted_fee().to_double());

            auto& wallData = getWalletData(initTransaction.target());
            wallData.balance_ -= initTransaction.amount();

            emit walletUpdateEvent(toPublicKey(initTransaction.source()), wallDataIniter);
            emit walletUpdateEvent(toPublicKey(initTransaction.target()), wallData);
        }
    }

    if (waitingSmart && !inverse) {
        auto& wallDataIniter = getWalletData(initTransaction.source());
        wallDataIniter.balance_ -= csdb::Amount(newStateTransaction.user_field(trx_uf::new_state::Fee).value<csdb::Amount>());
        wallDataIniter.balance_ -= csdb::Amount(initTransaction.counted_fee().to_double());
        wallDataIniter.balance_ += csdb::Amount(initTransaction.max_fee().to_double());
        wallDataIniter.balance_ -= csdb::Amount(newStateTransaction.counted_fee().to_double());

        auto& wallData = getWalletData(initTransaction.target());
        wallData.balance_ += initTransaction.amount();

        emit walletUpdateEvent(toPublicKey(initTransaction.source()), wallDataIniter);
        emit walletUpdateEvent(toPublicKey(initTransaction.target()), wallData);
    }
}

void WalletsCache::Updater::loadTrxForTarget(const csdb::Transaction& tr, bool inverse) {
    auto& wallData = getWalletData(tr.target());
    csdb::UserField ufld = tr.user_field(trx_uf::sp::delegated);
    if (!inverse) {
        if (ufld.is_valid()) {
            staking_->addDelegations(
                ufld,
                toPublicKey(tr.source()),
                toPublicKey(tr.target()),
                tr.amount(),
                tr.id()
            );
        }
        else {
            wallData.balance_ += tr.amount();
        }

#ifdef MONITOR_NODE
        setWalletTime(toPublicKey(tr.target()), tr.get_time());
#endif
        wallData.lastTransaction_ = tr.id();
    }
    else {
        if (ufld.is_valid()) {
            staking_->revertDelegations(
                ufld,
                toPublicKey(tr.source()),
                toPublicKey(tr.target()),
                tr.amount(),
                tr.id()
            );
        }
        else {
            wallData.balance_ -= tr.amount();
        }
    }

    if (tr.source() != tr.target()) { // Already counted in loadTrxForSource
        !inverse ? ++wallData.transNum_ : --wallData.transNum_;
    }

    emit walletUpdateEvent(toPublicKey(tr.target()), wallData);
}

void WalletsCache::Updater::updateLastTransactions(const std::vector<std::pair<PublicKey, csdb::TransactionID>>& updates) {
    for (const auto& u : updates) {
        auto it = data_.wallets_.find(u.first);
        if (it != data_.wallets_.end()) {
            it->second.lastTransaction_ = u.second;

            emit walletUpdateEvent(it->first, it->second);
        }
    }
}

void WalletsCache::iterateOverWallets(const std::function<bool(const PublicKey&, const WalletData&)> func) {
    for (const auto& wallet : wallets_) {
        if (!func(wallet.first, wallet.second)) {
            break;
        }
    }
}

#ifdef MONITOR_NODE
void WalletsCache::iterateOverWriters(const std::function<bool(const PublicKey&, const TrustedData&)> func) {
    for (const auto& wrd : trusted_info_) {
        if (!func(wrd.first, wrd.second)) {
            break;
        }
    }
}
#endif
}  // namespace cs
