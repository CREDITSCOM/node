#include <algorithm>
#include <blockchain.hpp>
#include <csdb/amount_commission.hpp>
#include <csnode/walletscache.hpp>
#include <csnode/walletsids.hpp>
#include <lib/system/logger.hpp>
#include <solver/smartcontracts.hpp>

namespace {
const uint8_t kUntrustedMarker = 255;
}  // namespace

namespace cs {
void WalletsCache::convert(const csdb::Address& address, WalletData::Address& walletAddress) const {
    if (address.is_public_key()) {
        walletAddress = address.public_key();
    }
    else {
        csdb::Address res;
        if (walletsIds_.normal().findaddr(address.wallet_id(), res)) {
            walletAddress = res.public_key();
        }
    }
}

void WalletsCache::convert(const WalletData::Address& walletAddress, csdb::Address& address) {
    address = csdb::Address::from_public_key(walletAddress);
}

WalletsCache::WalletsCache(const Config& config, csdb::Address genesisAddress, csdb::Address startAddress, WalletsIds& walletsIds)
: config_(config)
, walletsIds_(walletsIds)
, genesisAddress_(genesisAddress)
, startAddress_(startAddress) {
    wallets_.reserve(config.initialWalletsNum_);
}

WalletsCache::~WalletsCache() {
    for (auto el : wallets_)
        delete el;
}

std::unique_ptr<WalletsCache::Updater> WalletsCache::createUpdater() {
    return std::unique_ptr<Updater>(new Updater(*this));
}

// Updater
WalletsCache::Updater::Updater(WalletsCache& data)
: data_(data) {
    modified_.resize(data.wallets_.size(), false);
}

void WalletsCache::Updater::loadNextBlock(csdb::Pool& pool, const cs::ConfidantsKeys& confidants, const BlockChain& blockchain) {
    modified_.reset();

    csdb::Pool::Transactions& transactions = pool.transactions();
    csdb::Amount totalAmountOfCountedFee = 0;
#ifdef MONITOR_NODE
    auto wrWall = pool.writer_public_key();

    WalletData::Address addr;
    std::copy(wrWall.begin(), wrWall.end(), addr.begin());

    auto it_writer = data_.trusted_info_.find(addr);
    if (it_writer == data_.trusted_info_.end()) {
        auto res = data_.trusted_info_.insert(std::make_pair(addr, TrustedData()));
        it_writer = res.first;
    }
    ++it_writer->second.times;

    WalletData::Address addr_trusted;
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
    csdb::Address wallAddress = transaction.target();
    if (wallAddress == data_.genesisAddress_ || wallAddress == data_.startAddress_) {
        return;
    }
    WalletId id{};
    if (!findWalletId(wallAddress, id)) {
        cserror() << "Cannot find target wallet, target is " << wallAddress.to_string();
        return;
    }
    WalletData& wallData = getWalletData(id, wallAddress);
    wallData.balance_ -= transaction.amount();
    setModified(id);
    data_.smartPayableTransactions_.push_back(transaction.id());

    if (!SmartContracts::is_executable(transaction)) {
        WalletId sourceId{};
        csdb::Address sourceAddress = transaction.source();
        if (!findWalletId(sourceAddress, sourceId)) {
            cserror() << "Cannot find source wallet, source is " << sourceAddress.to_string();
            return;
        }
        WalletData& sourceWallData = getWalletData(sourceId, sourceAddress);
        sourceWallData.balance_ += csdb::Amount(transaction.counted_fee().to_double()) - csdb::Amount(transaction.max_fee().to_double());
        setModified(sourceId);
    }
}

void WalletsCache::Updater::smartSourceTransactionReleased(const csdb::Transaction& smartSourceTrx, const csdb::Transaction& initTrx) {
    csdb::Address smartSourceAddress = smartSourceTrx.source();
    csdb::Address initAddress = initTrx.source();
    auto countedFee = csdb::Amount(smartSourceTrx.counted_fee().to_double());
    WalletId smartId{};
    WalletId initId{};
    if (!findWalletId(smartSourceAddress, smartId)) {
        cserror() << "Cannot find source wallet, source is " << smartSourceAddress.to_string();
        return;
    }
    if (!findWalletId(initAddress, initId)) {
        cserror() << "Cannot find source wallet, source is " << initAddress.to_string();
        return;
    }
    WalletData& smartWallData = getWalletData(smartId, smartSourceAddress);
    smartWallData.balance_ += countedFee;
    WalletData& initWallData = getWalletData(initId, initAddress);
    initWallData.balance_ -= countedFee;

    setModified(smartId);
    setModified(initId);
}

void WalletsCache::Updater::rollbackExceededTimeoutContract(const csdb::Transaction& transaction, const WalletsCache::RefContractCall& ref,
                                                            const csdb::Amount& execFee /*= 0*/) {
    csdb::Address wallAddress = transaction.source();
    if (wallAddress == data_.genesisAddress_ || wallAddress == data_.startAddress_) {
        return;
    }

    WalletId id{};
    if (!findWalletId(wallAddress, id)) {
        cserror() << "Cannot find source wallet, source is " << wallAddress.to_string();
        return;
    }

    WalletData& wallData = getWalletData(id, wallAddress);
    wallData.balance_ += transaction.amount() + csdb::Amount(transaction.max_fee().to_double()) - csdb::Amount(transaction.counted_fee().to_double());

    if (SmartContracts::is_executable(transaction)) {
        auto it = data_.canceledSmarts_.find(transaction.target());
        if (it == data_.canceledSmarts_.end()) {
            data_.canceledSmarts_.insert(std::make_pair(transaction.target(), std::list< RefContractCall >{ ref }));
        }
        else {
            it->second.push_front( ref );
        }
    }
    else {
        auto it = std::find(data_.smartPayableTransactions_.cbegin(), data_.smartPayableTransactions_.cend(), transaction.id());
        if (it != data_.smartPayableTransactions_.cend()) {
            data_.smartPayableTransactions_.erase(it);
            wallData.balance_ -= execFee;
        }
    }

    setModified(id);
}

#ifdef MONITOR_NODE
bool WalletsCache::Updater::setWalletTime(const WalletData::Address& address, const uint64_t& p_timeStamp) {
    for (auto& it : data_.wallets_) {
        if (it != nullptr && it->address_ == address) {
            it->createTime_ = p_timeStamp;
            return true;
        }
    }
    return false;
}
#endif

void WalletsCache::Updater::fundConfidantsWalletsWithFee(const csdb::Amount& totalFee, const cs::ConfidantsKeys& confidants, const std::vector<uint8_t>& realTrusted) {
    if (!confidants.size()) {
        cslog() << "WALLETS CACHE>> NO CONFIDANTS";
        return;
    }
    int32_t realTrustedNumber = 0;
    for (const auto& trustedMarker : realTrusted) {
        if (trustedMarker != kUntrustedMarker) {
            ++realTrustedNumber;
        }
    }
    csdb::Amount feeToEachConfidant = totalFee / realTrustedNumber;
    csdb::Amount payedFee = 0;
    int32_t numPayedTrusted = 0;
    for (size_t i = 0; i < confidants.size(); ++i) {
        if (i < realTrusted.size() && realTrusted[i] != kUntrustedMarker) {
            WalletId confidantId{};
            csdb::Address confidantAddress = csdb::Address::from_public_key(confidants[i]);
            if (!findWalletId(confidantAddress, confidantId)) {
                cserror() << "Cannot find confidant wallet, source is " << confidantAddress.to_string();
                return;
            }
            WalletData& walletData = getWalletData(confidantId, confidantAddress);
            walletData.balance_ += feeToEachConfidant;

#ifdef MONITOR_NODE
            auto it_writer = data_.trusted_info_.find(walletData.address_);
            it_writer->second.totalFee += feeToEachConfidant;
#endif

            payedFee += feeToEachConfidant;
            ++numPayedTrusted;
            if (numPayedTrusted == (realTrustedNumber - 1)) {
                feeToEachConfidant = totalFee - payedFee;
            }
            setModified(confidantId);
        }
    }
}

void WalletsCache::Updater::fundConfidantsWalletsWithExecFee(const csdb::Transaction& transaction, const BlockChain& blockchain) {
    if (!SmartContracts::is_new_state(transaction)) {
        cswarning() << __func__ << ": transaction is not new state";
        return;
    }
    SmartContractRef smartRef(transaction.user_field(trx_uf::new_state::RefStart));
    if (!smartRef.is_valid()) {
        cserror() << __func__ << ": incorrect reference to starter transaction in new state";
        return;
    }
    csdb::Pool pool = blockchain.loadBlock(smartRef.sequence);
    if (!pool.is_valid()) {
        cserror() << __func__ << ": invalid pool";
        return;
    }
    const ConfidantsKeys& confidants = pool.confidants();
    const std::vector<uint8_t> realTrusted = cs::Utils::bitsToMask(pool.numberTrusted(), pool.realTrusted());
    int32_t realTrustedNumber = 0;  // int32_t due to csdb::Amount operator/
    for (const auto& trustedMarker : realTrusted) {
        if (trustedMarker != kUntrustedMarker) {
            ++realTrustedNumber;
        }
    }
    csdb::Amount feeToEachConfidant = transaction.user_field(trx_uf::new_state::Fee).value<csdb::Amount>() / realTrustedNumber;
    csdb::Amount payedFee = 0;
    int32_t numPayedTrusted = 0;
    for (size_t i = 0; i < confidants.size(); ++i) {
        if (i < realTrusted.size() && realTrusted[i] != kUntrustedMarker) {
            WalletId confidantId{};
            csdb::Address confidantAddress = csdb::Address::from_public_key(confidants[i]);
            if (!findWalletId(confidantAddress, confidantId)) {
                cserror() << "Cannot find confidant wallet, source is " << confidantAddress.to_string();
                return;
            }
            WalletData& walletData = getWalletData(confidantId, confidantAddress);
            walletData.balance_ += feeToEachConfidant;
            payedFee += feeToEachConfidant;
            ++numPayedTrusted;
            if (numPayedTrusted == (realTrustedNumber - 1)) {
                feeToEachConfidant = transaction.user_field(trx_uf::new_state::Fee).value<csdb::Amount>() - payedFee;
            }
            setModified(confidantId);
        }
    }
}

double WalletsCache::Updater::load(const csdb::Transaction& tr, const BlockChain& blockchain) {
    loadTrxForTarget(tr);
    return loadTrxForSource(tr, blockchain);
}

double WalletsCache::Updater::loadTrxForSource(const csdb::Transaction& tr, const BlockChain& blockchain) {
    csdb::Address wallAddress;
    bool smartIniter = false;
    csdb::Transaction initTransaction;
    cs::WalletsCache::RefContractCall initRef{ std::numeric_limits<cs::Sequence>::max(), std::numeric_limits<uint32_t>::max() };
    if (SmartContracts::is_new_state(tr)) {
        csdb::UserField fld = tr.user_field(cs::trx_uf::new_state::RefStart);
        if (fld.is_valid()) {
            cs::SmartContractRef ref(fld);
            csdb::Pool pool = blockchain.loadBlock(ref.sequence);
            if (pool.is_valid() && pool.transactions_count() > ref.transaction) {
                initTransaction = pool.transactions()[ref.transaction];
                wallAddress = initTransaction.source();
            }
            initRef.sequence = ref.sequence;
            initRef.transaction = uint32_t(ref.transaction);
        }
        smartIniter = true;
    }
    else {
        wallAddress = tr.source();
    }
    if (wallAddress == data_.genesisAddress_ || wallAddress == data_.startAddress_) {
        return 0;
    }

    WalletId id{};
    if (!findWalletId(wallAddress, id)) {
        cserror() << "Cannot find source wallet, source is " << wallAddress.to_string();
        return 0;
    }
    WalletData& wallData = getWalletData(id, tr.source());

    if (SmartContracts::is_executable(tr)) {
        wallData.balance_ -= csdb::Amount(tr.max_fee().to_double());
    }
    else if (SmartContracts::is_new_state(tr)) {
        WalletsCache::RefContractCall stateRef{ tr.id().pool_seq(), uint32_t(tr.id().index()) };
        if (isCanceledSmart(initTransaction.target(), initRef)) {
            cs::SmartContractRef ref(blockchain.getHashBySequence(initTransaction.id().pool_seq()), initRef.sequence, initRef.transaction);
            csdebug() << "WalletsCache: (deprecated behaviour) timeout was detected for " << ref
                << " before, new_state found in block " << WithDelimiters(stateRef.sequence);
            wallData.balance_ -= csdb::Amount(initTransaction.max_fee().to_double())
                               + csdb::Amount(initTransaction.counted_fee().to_double())
                               - initTransaction.amount();
        }
        checkCanceledSmart(initTransaction.target(), initRef);
        if (SmartContracts::is_executable(initTransaction)) {
            wallData.balance_ += csdb::Amount(initTransaction.max_fee().to_double()) - csdb::Amount(initTransaction.counted_fee().to_double()) -
                                 csdb::Amount(tr.counted_fee().to_double()) - csdb::Amount(tr.user_field(trx_uf::new_state::Fee).value<csdb::Amount>());
        }
        else {
            checkSmartWaitingForMoney(initTransaction, initRef, tr);
        }

        WalletId id_s{};
        if (!findWalletId(tr.source(), id_s)) {
            cserror() << "Cannot find source wallet, source is " << tr.source().to_string();
            return 0;
        }

		WalletData& wallData_s = getWalletData(id_s, tr.source());
		++wallData_s.transNum_;
        wallData_s.trxTail_.push(tr.innerID());
        cs::Bytes tmp;
        tmp.assign(wallData_s.address_.cbegin(), wallData_s.address_.cend());
        csdetails() << "Wallets: innerID of (new_state) " << EncodeBase58(tmp) << " <- " << tr.innerID();
        wallData_s.lastTransaction_ = tr.id();
        setModified(id_s);
    }
    else {
        wallData.balance_ -= csdb::Amount(tr.counted_fee().to_double());
    }

    if (!smartIniter) {
        wallData.balance_ -= tr.amount();
		++wallData.transNum_;
        wallData.trxTail_.push(tr.innerID());
        cs::Bytes tmp;
        tmp.assign(wallData.address_.cbegin(), wallData.address_.cend());
        csdetails() << "Wallets: innerID of " << EncodeBase58(tmp) << " <- " << tr.innerID();

#ifdef MONITOR_NODE        
        setWalletTime(wallData.address_, tr.get_time());
#endif
        wallData.lastTransaction_ = tr.id();
    }

    setModified(id);
    return tr.counted_fee().to_double();
}

bool WalletsCache::Updater::isCanceledSmart(const csdb::Address& contract_addr, const WalletsCache::RefContractCall& ref) {
    auto it = data_.canceledSmarts_.find(contract_addr);
    if (it == data_.canceledSmarts_.end()) {
        return false;
    }
    return it->second.end() != std::find_if(it->second.begin(), it->second.end(),
        [&](const RefContractCall& item) { return item.sequence == ref.sequence && item.transaction == ref.transaction; });
}

void WalletsCache::Updater::checkCanceledSmart(const csdb::Address& contract_addr, const WalletsCache::RefContractCall& ref) {
    auto it = data_.canceledSmarts_.find(contract_addr);
    if (it == data_.canceledSmarts_.end()) {
        return;
    }
    for (auto i = it->second.cbegin(); i != it->second.cend(); ++i) {
        if (i->sequence <= ref.sequence || (i->sequence == ref.sequence && i->transaction <= ref.transaction)) {
            it->second.erase(i, it->second.cend());
            break;
        }
    }
    if (it->second.empty()) {
        data_.canceledSmarts_.erase(it);
    }
}

void WalletsCache::Updater::checkSmartWaitingForMoney(const csdb::Transaction& initTransaction,
                                                      const WalletsCache::RefContractCall& initRef,
                                                      const csdb::Transaction& newStateTransaction) {
    if (!cs::SmartContracts::is_state_updated(newStateTransaction)) {
        csdb::Amount fee(0);
        csdb::UserField fld = newStateTransaction.user_field(trx_uf::new_state::Fee);
        if (fld.is_valid()) {
            fee = fld.value<csdb::Amount>();
        }
        rollbackExceededTimeoutContract(initTransaction, initRef);
        return;
    }
    bool waitingSmart = false;
    auto it = std::find(data_.smartPayableTransactions_.cbegin(), data_.smartPayableTransactions_.cend(), initTransaction.id());
    if (it != data_.smartPayableTransactions_.cend()) {
        data_.smartPayableTransactions_.erase(it);
        waitingSmart = true;
    }

    if (waitingSmart) {
        csdb::Address wallAddress = initTransaction.target();
        csdb::Address wallAddressIniter = initTransaction.source();
        if (wallAddress == data_.genesisAddress_ || wallAddress == data_.startAddress_ || wallAddressIniter == data_.genesisAddress_ || wallAddressIniter == data_.startAddress_) {
            return;
        }
        WalletId id{};
        WalletId sourceId{};
        if (!findWalletId(wallAddress, id)) {
            cserror() << "Cannot find target wallet, target is " << wallAddress.to_string();
            return;
        }
        if (!findWalletId(wallAddressIniter, sourceId)) {
            cserror() << "Cannot find source wallet, source is " << wallAddressIniter.to_string();
            return;
        }

        WalletData& wallDataIniter = getWalletData(sourceId, wallAddressIniter);
        wallDataIniter.balance_ -= csdb::Amount(newStateTransaction.user_field(trx_uf::new_state::Fee).value<csdb::Amount>());
        wallDataIniter.balance_ -= csdb::Amount(initTransaction.counted_fee().to_double());
        wallDataIniter.balance_ += csdb::Amount(initTransaction.max_fee().to_double());
        wallDataIniter.balance_ -= csdb::Amount(newStateTransaction.counted_fee().to_double());

        WalletData& wallData = getWalletData(id, wallAddress);
        wallData.balance_ += initTransaction.amount();

        setModified(id);
        setModified(sourceId);
    }
}

void WalletsCache::Updater::loadTrxForTarget(const csdb::Transaction& tr) {
    csdb::Address wallAddress = tr.target();

    if (wallAddress == data_.genesisAddress_ || wallAddress == data_.startAddress_)
        return;
    WalletId id{};
    if (!findWalletId(wallAddress, id)) {
        cserror() << "Cannot find target wallet, target is " << wallAddress.to_string();
        return;
    }

    WalletData& wallData = getWalletData(id, tr.target());

    wallData.balance_ += tr.amount();
    setModified(id);

    if (tr.source() != tr.target())  // Already counted in loadTrxForSource
        ++wallData.transNum_;

#ifdef MONITOR_NODE
    setWalletTime(wallData.address_, tr.get_time());
#endif
    wallData.lastTransaction_ = tr.id();
}

bool WalletsCache::Updater::findWalletId(const csdb::Address& address, WalletId& id) {
    if (!data_.walletsIds_.normal().find(address, id)) {
        return false;
    }
    return true;
}

WalletsCache::WalletData& WalletsCache::Updater::getWalletData(WalletId id, const csdb::Address& address) {
    if (id >= data_.wallets_.size()) {
        data_.wallets_.resize(id + 1);
    }

    if (!data_.wallets_[id]) {
        data_.wallets_[id] = new WalletData{};
        data_.convert(address, data_.wallets_[id]->address_);
    }

    return *data_.wallets_[id];
}

void WalletsCache::Updater::setModified(WalletId id) {
    modified_.resize(data_.wallets_.size());
    if (id >= modified_.size()) {
        cserror() << "id >= modified_.size: id=" << id << " modified_.size=" << modified_.size();
        return;
    }
    modified_.set(id);
}

const WalletsCache::WalletData* WalletsCache::Updater::findWallet(WalletId id) const {
    if (id >= data_.wallets_.size()) {
        return nullptr;
    }
    return data_.wallets_[id];
}

void WalletsCache::iterateOverWallets(const std::function<bool(const WalletData::Address&, const WalletData&)> func) {
    for (const auto& wdp : wallets_) {
        if (wdp != nullptr && !func(wdp->address_, *wdp)) {
            break;
        }
    }
}

#ifdef MONITOR_NODE
void WalletsCache::iterateOverWriters(const std::function<bool(const WalletData::Address&, const TrustedData&)> func) {
    for (const auto& wrd : trusted_info_) {
        if (!func(wrd.first, wrd.second)) {
            break;
        }
    }
}
#endif
}  // namespace cs
