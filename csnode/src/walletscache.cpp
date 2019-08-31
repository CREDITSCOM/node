#include <algorithm>

#include <blockchain.hpp>
#include <csdb/amount_commission.hpp>
#include <csnode/walletscache.hpp>
#include <csnode/walletsids.hpp>
#include <lib/system/logger.hpp>
#include <solver/smartcontracts.hpp>

namespace {
const uint8_t kUntrustedMarker = 255;

inline int32_t getReadTrustedNum(const std::vector<uint8_t>& realTrusted) {
    int32_t res = 0;

    for (auto trustedMarker : realTrusted) {
        if (trustedMarker != kUntrustedMarker) {
            ++res;
        }
    }
    return res;
}

const char* kLogPrefix = "WalletsCache: ";

}  // namespace

namespace cs {

WalletsCache::WalletsCache(WalletsIds& walletsIds) : walletsIds_(walletsIds) {}

std::unique_ptr<WalletsCache::Updater> WalletsCache::createUpdater() {
    return std::make_unique<Updater>(*this);
}

WalletsCache::Updater::Updater(WalletsCache& data) : data_(data) {}

PublicKey WalletsCache::Updater::toPublicKey(const csdb::Address& addr) const {
    csdb::Address res;
    if (addr.is_public_key() || !data_.walletsIds_.normal().findaddr(addr.wallet_id(), res)) {
        return addr.public_key();
    }
    return res.public_key();
}

void WalletsCache::Updater::loadNextBlock(csdb::Pool& pool, const cs::ConfidantsKeys& confidants, const BlockChain& blockchain) {
    csdb::Pool::Transactions& transactions = pool.transactions();
    csdb::Amount totalAmountOfCountedFee = 0;
#ifdef MONITOR_NODE
    auto wrWall = pool.writer_public_key();

    PublicKey addr;
    std::copy(wrWall.begin(), wrWall.end(), addr.begin());

    auto it_writer = data_.trusted_info_.find(addr);
    if (it_writer == data_.trusted_info_.end()) {
        auto res = data_.trusted_info_.insert(std::make_pair(addr, TrustedData()));
        it_writer = res.first;
    }
    ++it_writer->second.times;

    PublicKey addr_trusted;
    for (const auto& it : confidants) {
        std::copy(it.begin(), it.end(), addr_trusted.begin());
        auto it_trusted = data_.trusted_info_.find(addr_trusted);
        if (it_trusted == data_.trusted_info_.end()) {
            const auto res = data_.trusted_info_.insert(std::make_pair(addr_trusted, TrustedData()));
            it_trusted = res.first;
        }
        ++it_trusted->second.times_trusted;
    }
#endif

    for (auto itTrx = transactions.begin(); itTrx != transactions.end(); ++itTrx) {
        itTrx->set_time(pool.get_time());
        totalAmountOfCountedFee += load(*itTrx, blockchain);
        if (SmartContracts::is_new_state(*itTrx)) {
            fundConfidantsWalletsWithExecFee(*itTrx, blockchain);
        }
    }

    if (totalAmountOfCountedFee > csdb::Amount(0)) {
        fundConfidantsWalletsWithFee(totalAmountOfCountedFee, confidants, cs::Utils::bitsToMask(pool.numberTrusted(), pool.realTrusted()));
    }

    [[maybe_unused]] auto timeStamp = atoll(pool.user_field(0).value<std::string>().c_str());
#ifdef MONITOR_NODE
    setWalletTime(wrWall, timeStamp);
#endif
}

void WalletsCache::Updater::invokeReplenishPayableContract(const csdb::Transaction& transaction) {
    auto& wallData = getWalletData(transaction.target());

    wallData.balance_ -= transaction.amount();
    data_.smartPayableTransactions_.push_back(transaction.id());

    if (!SmartContracts::is_executable(transaction)) {
        auto& sourceWallData = getWalletData(transaction.source());
        sourceWallData.balance_ += csdb::Amount(transaction.counted_fee().to_double())
                                 - csdb::Amount(transaction.max_fee().to_double());
    }
}

void WalletsCache::Updater::smartSourceTransactionReleased(const csdb::Transaction& smartSourceTrx, const csdb::Transaction& initTrx) {
    auto countedFee = csdb::Amount(smartSourceTrx.counted_fee().to_double());

    auto& smartWallData = getWalletData(smartSourceTrx.source());
    smartWallData.balance_ += countedFee;

    auto& initWallData = getWalletData(initTrx.source());
    initWallData.balance_ -= countedFee;
}

void WalletsCache::Updater::rollbackExceededTimeoutContract(const csdb::Transaction& transaction, const csdb::Amount& execFee) {
    auto& wallData = getWalletData(transaction.source());
    wallData.balance_ += transaction.amount()
                       + csdb::Amount(transaction.max_fee().to_double())
                       - csdb::Amount(transaction.counted_fee().to_double());

    if (SmartContracts::is_executable(transaction)) {
        auto it = data_.canceledSmarts_.find(transaction.target());
        if (it == data_.canceledSmarts_.end()) {
            data_.canceledSmarts_.insert(std::make_pair(transaction.target(), std::list<csdb::TransactionID>{transaction.id()}));
        }
        else {
            it->second.push_front(transaction.id());
        }
    }
    else {
        auto it = std::find(data_.smartPayableTransactions_.cbegin(), data_.smartPayableTransactions_.cend(), transaction.id());
        if (it != data_.smartPayableTransactions_.cend()) {
            data_.smartPayableTransactions_.erase(it);
            wallData.balance_ -= execFee;
        }
    }
}

#ifdef MONITOR_NODE
bool WalletsCache::Updater::setWalletTime(const PublicKey& address, const uint64_t& p_timeStamp) {
    for (auto& it : data_.wallets_) {
        if (it.first == address) {
            it.second.createTime_ = p_timeStamp;
            return true;
        }
    }
    return false;
}
#endif

void WalletsCache::Updater::fundConfidantsWalletsWithFee(const csdb::Amount& totalFee,
                                                         const cs::ConfidantsKeys& confidants,
                                                         const std::vector<uint8_t>& realTrusted) {
    if (!confidants.size()) {
        cslog() << kLogPrefix << "NO CONFIDANTS";
        return;
    }
    auto realTrustedNumber = getReadTrustedNum(realTrusted);
    csdb::Amount feeToEachConfidant = totalFee / realTrustedNumber;
    csdb::Amount payedFee = 0;
    int32_t numPayedTrusted = 0;
    for (size_t i = 0; i < confidants.size(); ++i) {
        if (i < realTrusted.size() && realTrusted[i] != kUntrustedMarker) {
            auto& walletData = getWalletData(confidants[i]);
            walletData.balance_ += feeToEachConfidant;

#ifdef MONITOR_NODE
            auto it_writer = data_.trusted_info_.find(confidants[i]);
            it_writer->second.totalFee += feeToEachConfidant;
#endif

            payedFee += feeToEachConfidant;
            ++numPayedTrusted;
            if (numPayedTrusted == (realTrustedNumber - 1)) {
                feeToEachConfidant = totalFee - payedFee;
            }
        }
    }
}

void WalletsCache::Updater::fundConfidantsWalletsWithExecFee(const csdb::Transaction& transaction, const BlockChain& blockchain) {
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
    const ConfidantsKeys& confidants = pool.confidants();
    const std::vector<uint8_t> realTrusted = cs::Utils::bitsToMask(pool.numberTrusted(), pool.realTrusted());
    auto realTrustedNumber = getReadTrustedNum(realTrusted);
    csdb::Amount feeToEachConfidant = transaction.user_field(trx_uf::new_state::Fee).value<csdb::Amount>() / realTrustedNumber;
    csdb::Amount payedFee = 0;
    int32_t numPayedTrusted = 0;
    for (size_t i = 0; i < confidants.size(); ++i) {
        if (i < realTrusted.size() && realTrusted[i] != kUntrustedMarker) {
            WalletData& walletData = getWalletData(confidants[i]);
            walletData.balance_ += feeToEachConfidant;
            payedFee += feeToEachConfidant;
            ++numPayedTrusted;
            if (numPayedTrusted == (realTrustedNumber - 1)) {
                feeToEachConfidant = transaction.user_field(trx_uf::new_state::Fee).value<csdb::Amount>() - payedFee;
            }
        }
    }
}

double WalletsCache::Updater::loadTrxForSource(const csdb::Transaction& tr, const BlockChain& blockchain) {
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

    auto& wallData = getWalletData(wallAddress);

    if (SmartContracts::is_executable(tr)) {
        wallData.balance_ -= csdb::Amount(tr.max_fee().to_double());
    }
    else if (SmartContracts::is_new_state(tr)) {
        if (!initTransaction.is_valid()) {
            const auto id = tr.id();
            csdebug() << kLogPrefix << "failed to load init transaction for new state " << FormatRef(id.pool_seq(), (size_t)id.index());
        }
        else {
            const auto start_id = initTransaction.id();
            const auto start_target = initTransaction.target();
            const double start_max_fee = initTransaction.max_fee().to_double();
            const double start_counted_fee = initTransaction.counted_fee().to_double();
            if (isCanceledSmart(start_target, start_id)) {
                csdebug() << "WalletsCache: (deprecated behaviour) timeout was detected for " << FormatRef(start_id.pool_seq(), (size_t )start_id.index())
                    << " before, new_state found in block " << WithDelimiters(start_id.pool_seq());
                wallData.balance_ -= (csdb::Amount(start_max_fee) + csdb::Amount(start_counted_fee) - initTransaction.amount());
            }
            checkCanceledSmart(start_target, start_id);
            if (SmartContracts::is_executable(initTransaction)) {
                wallData.balance_ += csdb::Amount(start_max_fee) - csdb::Amount(start_counted_fee) -
                    csdb::Amount(tr.counted_fee().to_double()) - csdb::Amount(tr.user_field(trx_uf::new_state::Fee).value<csdb::Amount>());
            }
            else {
                checkSmartWaitingForMoney(initTransaction, tr);
            }
        }
		auto& wallData_s = getWalletData(tr.source());
		++wallData_s.transNum_;
        wallData_s.trxTail_.push(tr.innerID());
        wallData_s.lastTransaction_ = tr.id();

        auto pubKey = toPublicKey(tr.source());
        csdetails() << "Wallets: innerID of (new_state) "
                    << EncodeBase58(cs::Bytes(pubKey.begin(), pubKey.end()))
                    << " <- " << tr.innerID();
    }
    else {
        wallData.balance_ -= csdb::Amount(tr.counted_fee().to_double());
    }

    if (!smartIniter) {
        wallData.balance_ -= tr.amount();
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

void WalletsCache::Updater::checkCanceledSmart(const csdb::Address& contract_addr, const csdb::TransactionID& tid) {
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
                                                      const csdb::Transaction& newStateTransaction) {
    if (!cs::SmartContracts::is_state_updated(newStateTransaction)) {
        csdb::Amount fee(0);
        csdb::UserField fld = newStateTransaction.user_field(trx_uf::new_state::Fee);
        if (fld.is_valid()) {
            fee = fld.value<csdb::Amount>();
        }
        rollbackExceededTimeoutContract(initTransaction, fee);
        return;
    }
    bool waitingSmart = false;
    auto it = std::find(data_.smartPayableTransactions_.cbegin(), data_.smartPayableTransactions_.cend(), initTransaction.id());
    if (it != data_.smartPayableTransactions_.cend()) {
        data_.smartPayableTransactions_.erase(it);
        waitingSmart = true;
    }

    if (waitingSmart) {
        auto& wallDataIniter = getWalletData(initTransaction.source());
        wallDataIniter.balance_ -= csdb::Amount(newStateTransaction.user_field(trx_uf::new_state::Fee).value<csdb::Amount>())
                                 - csdb::Amount(initTransaction.counted_fee().to_double())
                                 + csdb::Amount(initTransaction.max_fee().to_double())
                                 - csdb::Amount(newStateTransaction.counted_fee().to_double());

        auto& wallData = getWalletData(initTransaction.target());
        wallData.balance_ += initTransaction.amount();
    }
}

void WalletsCache::Updater::loadTrxForTarget(const csdb::Transaction& tr) {
    auto& wallData = getWalletData(tr.target());
    wallData.balance_ += tr.amount();

    if (tr.source() != tr.target()) { // Already counted in loadTrxForSource
        ++wallData.transNum_;
    }

#ifdef MONITOR_NODE
    setWalletTime(toPublicKey(tr.target()), tr.get_time());
#endif
    wallData.lastTransaction_ = tr.id();
}

void WalletsCache::Updater::updateLastTransactions(const std::vector<std::pair<PublicKey, csdb::TransactionID>>& updates) {
    for (const auto& u : updates) {
        auto it = std::find_if(data_.wallets_.begin(), data_.wallets_.end(), [&u](auto& wall) {
                               return wall.first == u.first; });
        if (it != data_.wallets_.end()) {
            it->second.lastTransaction_ = u.second;
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
