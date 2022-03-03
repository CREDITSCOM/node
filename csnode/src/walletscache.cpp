#include <algorithm>
#include <map>

#include <blockchain.hpp>
#include <csdb/amount_commission.hpp>
#include <csnode/multiwallets.hpp>
#include <csnode/staking.hpp>
#include <csnode/walletscache.hpp>
#include <csnode/walletsids.hpp>
#include <lib/system/logger.hpp>
#include <solver/smartcontracts.hpp>
#include <solver/consensus.hpp>
#include <csnode/configholder.hpp>

namespace {
const uint8_t kUntrustedMarker = 255;
const char* kLogPrefix = "WalletsCache: ";
}  // namespace

namespace cs {

WalletsCache::WalletsCache(WalletsIds& walletsIds) : walletsIds_(walletsIds) {
    multiWallets_ = std::make_unique<MultiWallets>();
    staking_ = std::make_unique<Staking>(
      [this](const PublicKey& k) -> WalletData {
          WalletData data;
          data.key_ = k;
          multiWallets_->getWalletData(data);
          return data;
      },
      [this](const WalletsCache::WalletData& wallet) {
          multiWallets_->onWalletCacheUpdated(wallet);
      }
    );
}

WalletsCache::~WalletsCache() = default;

uint64_t WalletsCache::getCount() const {
    return multiWallets_->size();
}

std::unique_ptr<WalletsCache::Updater> WalletsCache::createUpdater() {
    return std::make_unique<Updater>(*this);
}

WalletsCache::Updater::Updater(WalletsCache& data)
  : data_(data) {}

WalletsCache::Updater::~Updater() = default;

std::unique_ptr<WalletsCache::WalletData> WalletsCache::Updater::findWallet(const PublicKey& key) const {
    WalletsCache::WalletData data;
    data.key_ = key;
    if (!data_.multiWallets_->getWalletData(data)) {
        return nullptr;
    }
    return std::make_unique<WalletsCache::WalletData>(data);
}

std::unique_ptr<WalletsCache::WalletData> WalletsCache::Updater::findWallet(const csdb::Address& addr) const {
  return findWallet(toPublicKey(addr));
}

WalletsCache::WalletData WalletsCache::Updater::getWalletData(const PublicKey& key) {
  WalletsCache::WalletData data;
  data.key_ = key;
  data_.multiWallets_->getWalletData(data);
  return data;
}

WalletsCache::WalletData WalletsCache::Updater::getWalletData(const csdb::Address& addr) {
  return getWalletData(toPublicKey(addr));
}

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

    const auto& conf = cs::ConfigHolder::instance().config();
    if (conf->getBalanceChangeFlag() && conf->getBalanceChangeKey() != cs::Zero::key) {
        showBalanceChange_ = true;
        showBalanceChangeKey_ = conf->getBalanceChangeKey();
    }

    auto& transactions = pool.transactions();
    csdb::Amount totalAmountOfCountedFee = 0;
    //if (!transactions.empty()) {
    //    csdebug() << "Start block: " << pool.sequence();
    //}
    for (auto itTrx = transactions.begin(); itTrx != transactions.end(); ++itTrx) {
        //csdebug() << "Start transaction: " << itTrx->id().to_string();
        totalAmountOfCountedFee += load(*itTrx, blockchain, inverse);
        if (SmartContracts::is_new_state(*itTrx)) {
            //csdebug() << "Start conf funding for execution";
            fundConfidantsWalletsWithExecFee(*itTrx, blockchain, inverse);
            //csdebug() << "Finish conf funding for execution";
        }
        //csdebug() << "Finish transaction";
    }

    if (totalAmountOfCountedFee > csdb::Amount(0)) {
        fundConfidantsWalletsWithFee(totalAmountOfCountedFee, confidants,
                                     cs::Utils::bitsToMask(pool.numberTrusted(),
                                     pool.realTrusted()), inverse);
    }

    data_.staking_->cleanObsoletteDelegations(BlockChain::getBlockTime(pool));

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
/* @TODO optimize checkWallets - takes 96% of time during db loading
    if (!transactions.empty()) {
        csdebug() << "Finish block, total caches: " << data_.multiWallets_->checkWallets().to_string();
    }
*/
}

void WalletsCache::Updater::invokeReplenishPayableContract(const csdb::Transaction& transaction, bool inverse /* = false */) {
    auto wallData = getWalletData(transaction.target());

    if (!inverse) {
        wallData.balance_ -= transaction.amount();
        logOperation("Payable contract cost ", wallData.key_, transaction.amount());
        data_.smartPayableTransactions_.push_back(transaction.id());
    }
    else {
        wallData.balance_ += transaction.amount();
        data_.smartPayableTransactions_.remove(transaction.id());
    }

    if (!SmartContracts::is_executable(transaction)) {
        auto sourceWallData = getWalletData(transaction.source());
        if (!inverse) {
            sourceWallData.balance_ += csdb::Amount(transaction.counted_fee().to_double());
            logOperation("Counted fee +", sourceWallData.key_, csdb::Amount(transaction.counted_fee().to_double()));
            sourceWallData.balance_ -= csdb::Amount(transaction.max_fee().to_double()); 
            logOperation("Max fee -", sourceWallData.key_, csdb::Amount(transaction.max_fee().to_double()));
        }
        else {
            sourceWallData.balance_ -= csdb::Amount(transaction.counted_fee().to_double());
            sourceWallData.balance_ += csdb::Amount(transaction.max_fee().to_double());
        }

        data_.multiWallets_->onWalletCacheUpdated(sourceWallData);
    }

    data_.multiWallets_->onWalletCacheUpdated(wallData);
}

void WalletsCache::Updater::smartSourceTransactionReleased(const csdb::Transaction& smartSourceTrx,
                                                           const csdb::Transaction& initTrx,
                                                           bool inverse /* = false */) {
    auto countedFee = csdb::Amount(smartSourceTrx.counted_fee().to_double());

    auto smartWallData = getWalletData(smartSourceTrx.source());
    auto initWallData = getWalletData(initTrx.source());

    if (!inverse) {
        smartWallData.balance_ += countedFee;
        logOperation("Smart balance increase +", smartWallData.key_, countedFee);
        initWallData.balance_ -= countedFee;
        logOperation("Initiators balance decrease -", initWallData.key_, countedFee);
    }
    else {
        smartWallData.balance_ -= countedFee;
        initWallData.balance_ += countedFee;
    }

    data_.multiWallets_->onWalletCacheUpdated(smartWallData);
    data_.multiWallets_->onWalletCacheUpdated(initWallData);
}

void WalletsCache::Updater::rollbackExceededTimeoutContract(const csdb::Transaction& transaction,
                                                            const csdb::Amount& execFee,
                                                            bool inverse /* = false */) {
    auto wallData = getWalletData(transaction.source());
    if (!inverse) {
        wallData.balance_ += transaction.amount();
        logOperation("Rollback exceeded timeout for contract +", transaction.source().public_key(), transaction.amount());
        wallData.balance_ += csdb::Amount(transaction.max_fee().to_double());
        logOperation("Rollback max fee +", transaction.source().public_key(), transaction.amount());
        wallData.balance_ -= csdb::Amount(transaction.counted_fee().to_double());
        logOperation("Rollback counted fee -", transaction.source().public_key(), transaction.amount());
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
            logOperation("Rollback exec fee -", transaction.source().public_key(), execFee);
        }
        else {
            if (inverse) {
                data_.smartPayableTransactions_.push_back(transaction.id());
                wallData.balance_ += execFee;
            }
        }
    }

    data_.multiWallets_->onWalletCacheUpdated(wallData);
}

#ifdef MONITOR_NODE
bool WalletsCache::Updater::setWalletTime(const PublicKey& address, const uint64_t& p_timeStamp) {

    auto wData = getWalletData(address);

    if (wData.createTime_ == 0) {
        wData.createTime_ = p_timeStamp;
    }
    data_.multiWallets_->onWalletCacheUpdated(wData);
    return true;
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
    int32_t realTrustedNumber = 0;

    for (size_t i = 0; i < confidants.size() && i < realTrusted.size(); ++i) {
        if (realTrusted[i] == kUntrustedMarker) {
            continue;
        }
        ++realTrustedNumber;
        auto wallet = getWalletData(confidants[i]);
        totalStake += wallet.balance_;
        confidantAndStake[confidants[i]] += wallet.balance_;

        auto miningDelegations = data_.staking_->getMiningDelegations(confidants[i]);
        if (!miningDelegations) {
            continue;
        }

        for (auto& keyAndStake : *miningDelegations) {
            confidantAndStake[keyAndStake.first] += keyAndStake.second.amount;
            totalStake += keyAndStake.second.amount;
        }
    }
    csdb::Amount feeWithMining = totalFee  + totalFee * Consensus::miningCoefficient + Consensus::blockReward;
    csdb::Amount onePartOfFee = Consensus::stakingOn ? feeWithMining / totalStake : feeWithMining/realTrustedNumber;
    csdb::Amount payedFee = 0;
    size_t numPayedTrusted = 0;

    for (auto& confAndStake : confidantAndStake) {
            auto walletData = getWalletData(confAndStake.first);
            csdb::Amount feeToPay = 0; 

            if (numPayedTrusted == confidantAndStake.size() - 1) {
                feeToPay = feeWithMining - payedFee;
            }
            else {
                feeToPay = Consensus::stakingOn ? onePartOfFee * confAndStake.second : onePartOfFee;
            }

            if (!inverse) {
                walletData.balance_ += feeToPay;
                logOperation("Confidant fee added: +", confAndStake.first, feeToPay);
            }
            else {
                walletData.balance_ -= feeToPay;
                logOperation("Confidant fee reverted: -", confAndStake.first, feeToPay);
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

            data_.multiWallets_->onWalletCacheUpdated(walletData);
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

void WalletsCache::Updater::logOperation(const std::string& reason, const cs::PublicKey& key, const csdb::Amount& sum) {
    if (showBalanceChange_ && key == showBalanceChangeKey_) {
        csdebug() << reason << sum.to_string();
    }
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

    auto wallData = getWalletData(wallAddress);
    bool alreadyUpdated = false;

    if (SmartContracts::is_executable(tr)) {
        if (!inverse) {
            wallData.balance_ -= csdb::Amount(tr.max_fee().to_double());
            logOperation("Fee paid : -", tr.source().public_key(), csdb::Amount(tr.max_fee().to_double()));
        }
        else {
            wallData.balance_ += csdb::Amount(tr.max_fee().to_double());
            logOperation("Fee pay reverted: +", tr.source().public_key(), csdb::Amount(tr.max_fee().to_double()));
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
                    logOperation("Sum paid : -", wallData.key_, initTransaction.amount());
                    wallData.balance_ -= start_max_fee;
                    logOperation("Max fee : -", wallData.key_, start_max_fee);
                    wallData.balance_ += start_counted_fee;
                    logOperation("Start counted fee : +", wallData.key_, start_counted_fee);
                }
                else {
                    wallData.balance_ += initTransaction.amount();
                    logOperation("Sum paid (inversed): +", wallData.key_, initTransaction.amount());
                    wallData.balance_ += start_max_fee;
                    logOperation("Max fee (inversed): +", wallData.key_, start_max_fee);
                    wallData.balance_ -= start_counted_fee;
                    logOperation("Start counted fee (inversed): -", wallData.key_, start_counted_fee);
                }
            }
            checkCanceledSmart(start_target, start_id, inverse);
            if (SmartContracts::is_executable(initTransaction)) {
                if (!inverse) {
                    wallData.balance_ += start_max_fee;
                    logOperation("Max fee : +", wallData.key_, start_max_fee);
                    wallData.balance_ -= start_counted_fee;
                    logOperation("Start counted fee : -", wallData.key_, start_counted_fee);
                    wallData.balance_ -= csdb::Amount(tr.counted_fee().to_double());
                    logOperation("Counted fee : -", wallData.key_, csdb::Amount(tr.counted_fee().to_double()));
                    wallData.balance_ -= csdb::Amount(tr.user_field(trx_uf::new_state::Fee).value<csdb::Amount>());
                    logOperation("Execute fee : -", wallData.key_, csdb::Amount(tr.user_field(trx_uf::new_state::Fee).value<csdb::Amount>()));
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
                alreadyUpdated = true;
            }
        }
        auto wallData_s = getWalletData(tr.source());
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

        data_.multiWallets_->onWalletCacheUpdated(wallData_s);
    }
    else {
        if (!inverse) {
            wallData.balance_ -= csdb::Amount(tr.counted_fee().to_double());
            logOperation("Fee paid : -", tr.source().public_key(), csdb::Amount(tr.counted_fee().to_double()));
        }
        else {
            wallData.balance_ += csdb::Amount(tr.counted_fee().to_double());
            logOperation("Fee paid : +", tr.source().public_key(), csdb::Amount(tr.counted_fee().to_double()));
        }
    }
	//wallData = sources Account

    if (!smartIniter) {
        csdb::UserField ufld = tr.user_field(trx_uf::sp::delegated);
        if (!inverse) {
            if (ufld.is_valid()) {
                //data_.staking_->addDelegationsForSource(
                //    ufld,
                //    toPublicKey(tr.source()),
                //    toPublicKey(tr.target()),
                //    tr.amount()
                //);
                //alreadyUpdated = true;
            }
            else {
                wallData.balance_ -= tr.amount();
                logOperation("Transaction " + tr.id().to_string() + " send: -", tr.source().public_key(), tr.amount());
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
            if (ufld.is_valid()) {
                //data_.staking_->revertDelegationsForSource(
                //    ufld,
                //    toPublicKey(tr.source()),
                //    toPublicKey(tr.target()),
                //    tr.amount(),
                //    tr.id()
                //);
		        //alreadyUpdated = true;
            }
            else {
                wallData.balance_ += tr.amount();
                logOperation("Transaction " + tr.id().to_string() + " send (reverted): +", wallData.key_, tr.amount());
            }
            --wallData.transNum_;
        }
    }

    if (inverse) {
        if (smartIniter) {
            wallAddress = tr.source();
        }
        auto wallData_s = getWalletData(wallAddress);
        auto pubKey = toPublicKey(wallAddress);
        csdetails() << "Wallets: erase innerID of "
            << EncodeBase58(cs::Bytes(pubKey.begin(), pubKey.end()))
            << " -> " << tr.innerID();
        wallData_s.trxTail_.erase(tr.innerID());
        data_.multiWallets_->onWalletCacheUpdated(wallData_s);
    }

    if(!alreadyUpdated){
        data_.multiWallets_->onWalletCacheUpdated(wallData);
    }

    if (!smartIniter) {
        csdb::UserField ufld = tr.user_field(trx_uf::sp::delegated);
        if (!inverse) {
            if (ufld.is_valid()) {
                data_.staking_->addDelegationsForSource(
                    ufld,
                    toPublicKey(tr.source()),
                    toPublicKey(tr.target()),
                    tr.amount()
                );
            }
        }
        else {
            if (ufld.is_valid()) {
                data_.staking_->revertDelegationsForSource(
                    ufld,
                    toPublicKey(tr.source()),
                    toPublicKey(tr.target()),
                    tr.amount(),
                    tr.id()
                );
            }
        }
    }
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
        auto wallDataIniter = getWalletData(initTransaction.source());
        if (!inverse) {
            wallDataIniter.balance_ -= csdb::Amount(newStateTransaction.counted_fee().to_double());
            logOperation("Contract counted fee : -", initTransaction.source().public_key(), csdb::Amount(newStateTransaction.counted_fee().to_double()));
        }
        else {
            wallDataIniter.balance_ += csdb::Amount(newStateTransaction.counted_fee().to_double());
            logOperation("Contract counted fee (reverted): +", initTransaction.source().public_key(), csdb::Amount(newStateTransaction.counted_fee().to_double()));
        }
        data_.multiWallets_->onWalletCacheUpdated(wallDataIniter);
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

            auto wallDataIniter = getWalletData(initTransaction.source());
            wallDataIniter.balance_ += csdb::Amount(newStateTransaction.user_field(trx_uf::new_state::Fee).value<csdb::Amount>());
            wallDataIniter.balance_ += csdb::Amount(initTransaction.counted_fee().to_double());
            wallDataIniter.balance_ -= csdb::Amount(initTransaction.max_fee().to_double());
            wallDataIniter.balance_ += csdb::Amount(newStateTransaction.counted_fee().to_double());

            auto wallData = getWalletData(initTransaction.target());
            wallData.balance_ -= initTransaction.amount();

            data_.multiWallets_->onWalletCacheUpdated(wallDataIniter);
            data_.multiWallets_->onWalletCacheUpdated(wallData);
        }
    }

    if (waitingSmart && !inverse) {
        auto wallDataIniter = getWalletData(initTransaction.source());
        wallDataIniter.balance_ -= csdb::Amount(newStateTransaction.user_field(trx_uf::new_state::Fee).value<csdb::Amount>());
        logOperation("Transaction " + initTransaction.id().to_string() + ": Execution fee : -", initTransaction.source().public_key(), csdb::Amount(newStateTransaction.user_field(trx_uf::new_state::Fee).value<csdb::Amount>()));
        wallDataIniter.balance_ -= csdb::Amount(initTransaction.counted_fee().to_double());
        logOperation("Transaction " + initTransaction.id().to_string() + ": Init transaction counted fee : -", initTransaction.source().public_key(), csdb::Amount(initTransaction.counted_fee().to_double()));
        wallDataIniter.balance_ += csdb::Amount(initTransaction.max_fee().to_double());
        logOperation("Transaction " + initTransaction.id().to_string() + ": Init transaction max fee : +", initTransaction.source().public_key(), csdb::Amount(initTransaction.max_fee().to_double()));
        wallDataIniter.balance_ -= csdb::Amount(newStateTransaction.counted_fee().to_double());
        logOperation("Transaction " + initTransaction.id().to_string() + ": New state counted fee : -", initTransaction.source().public_key(), csdb::Amount(newStateTransaction.counted_fee().to_double()));

        auto wallData = getWalletData(initTransaction.target());
        wallData.balance_ += initTransaction.amount();
        logOperation("Init transactions +", wallData.key_, initTransaction.amount());

        data_.multiWallets_->onWalletCacheUpdated(wallDataIniter);
        data_.multiWallets_->onWalletCacheUpdated(wallData);
    }
}

void WalletsCache::Updater::loadTrxForTarget(const csdb::Transaction& tr, bool inverse) {
    auto wallData = getWalletData(tr.target());
    csdb::UserField ufld = tr.user_field(trx_uf::sp::delegated);
    //bool alreadyUpdated = false;
    if (!inverse) {
        if (ufld.is_valid()) {
            //data_.staking_->addDelegationsForTarget(
            //    ufld,
            //    toPublicKey(tr.source()),
            //    toPublicKey(tr.target()),
            //    tr.amount(),
            //    tr.id()
            //);
        }
        else {
            wallData.balance_ += tr.amount();
            auto addr = tr.target().public_key();
            logOperation("Transaction " + tr.id().to_string() + " received: ", addr, tr.amount());
        }

#ifdef MONITOR_NODE
        setWalletTime(toPublicKey(tr.target()), tr.get_time());
#endif
        wallData.lastTransaction_ = tr.id();
    }
    else {
        if (ufld.is_valid()) {
            //data_.staking_->revertDelegationsForTarget(
            //    ufld,
            //    toPublicKey(tr.source()),
            //    toPublicKey(tr.target()),
            //    tr.amount(),
            //    tr.id()
            //);
        }
        else {
            wallData.balance_ -= tr.amount();
            auto addr = tr.target().public_key();
            if (showBalanceChange_ && addr == showBalanceChangeKey_) {
                csdebug() << "Transaction " << tr.id().to_string() << " send (reverted): -" << tr.amount().to_string();
            }
        }
    }

    if (tr.source() != tr.target()) { // Already counted in loadTrxForSource
        !inverse ? ++wallData.transNum_ : --wallData.transNum_;
    }
    
    data_.multiWallets_->onWalletCacheUpdated(wallData);

    if (!inverse) {
        if (ufld.is_valid()) {
            data_.staking_->addDelegationsForTarget(
                ufld,
                toPublicKey(tr.source()),
                toPublicKey(tr.target()),
                tr.amount(),
                tr.id()
            );
            //alreadyUpdated = true;
        }
    }
    else {
        if (ufld.is_valid()) {
            data_.staking_->revertDelegationsForTarget(
                ufld,
                toPublicKey(tr.source()),
                toPublicKey(tr.target()),
                tr.amount(),
                tr.id()
            );
        }
    }
}

void WalletsCache::Updater::updateLastTransactions(const std::vector<std::pair<PublicKey, csdb::TransactionID>>& updates) {
    for (const auto& u : updates) {
        WalletsCache::WalletData wallet;
        wallet.key_ = u.first;
        if (!data_.multiWallets_->getWalletData(wallet)) {
            continue;
        }

        wallet.lastTransaction_ = u.second;
        data_.multiWallets_->onWalletCacheUpdated(wallet);
    }
}

void WalletsCache::iterateOverWallets(const std::function<bool(const PublicKey&, const WalletData&)> func) {
    multiWallets_->iterate(func);
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
