#include <algorithm>
#include <blockchain.hpp>
#include <csdb/amount_commission.hpp>
#include <csnode/walletscache.hpp>
#include <csnode/walletsids.hpp>
#include <lib/system/logger.hpp>
#include <solver/smartcontracts.hpp>

using namespace std;

namespace {
const uint8_t kUntrustedMarker = 255;

}  // namespace

namespace cs {
void WalletsCache::convert(const csdb::Address& address, WalletData::Address& walletAddress) {
    walletAddress = address.public_key();
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

std::unique_ptr<WalletsCache::Initer> WalletsCache::createIniter() {
    return std::unique_ptr<Initer>(new Initer(*this));
}

std::unique_ptr<WalletsCache::Updater> WalletsCache::createUpdater() {
    return std::unique_ptr<Updater>(new Updater(*this));
}

// Initer
WalletsCache::Initer::Initer(WalletsCache& data)
: ProcessorBase(data) {
    walletsSpecial_.reserve(data_.config_.initialWalletsNum_);
}

void WalletsCache::Initer::loadPrevBlock(csdb::Pool& curr, const cs::ConfidantsKeys& confidants, const BlockChain& blockchain) {
    load(curr, confidants, blockchain);
}

// Updater
WalletsCache::Updater::Updater(WalletsCache& data)
: ProcessorBase(data) {
    modified_.resize(data.wallets_.size(), false);
}

void WalletsCache::Updater::loadNextBlock(csdb::Pool& curr, const cs::ConfidantsKeys& confidants, const BlockChain& blockchain) {
    modified_.reset();
    load(curr, confidants, blockchain);
}

void WalletsCache::ProcessorBase::invokeReplenishPayableContract(const csdb::Transaction& transaction) {
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
    data_.smartPayableTransactions_.push_back(transaction);

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

void WalletsCache::ProcessorBase::smartSourceTransactionReleased(const csdb::Transaction& smartSourceTrx, const csdb::Transaction& initTrx) {
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

void WalletsCache::ProcessorBase::rollbackReplenishPayableContract(const csdb::Transaction& transaction, const csdb::Amount& execFee) {
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
        data_.closedSmarts_.push_back(transaction);
    }
    else {
        for (auto it = data_.smartPayableTransactions_.begin(); it != data_.smartPayableTransactions_.end(); it++) {
            if (it->source() == transaction.source() && it->innerID() == transaction.innerID()) {
                data_.smartPayableTransactions_.erase(it);
                wallData.balance_ -= execFee;
                break;
            }
        }
    }

    setModified(id);
}

// ProcessorBase
void WalletsCache::ProcessorBase::load(csdb::Pool& pool, const cs::ConfidantsKeys& confidants, const BlockChain& blockchain) {
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
#ifdef MONITOR_NODE
bool WalletsCache::ProcessorBase::setWalletTime(const WalletData::Address& address, const uint64_t& p_timeStamp) {
    for (auto& it : data_.wallets_) {
        if (it != nullptr && it->address_ == address) {
            it->createTime_ = p_timeStamp;
            return true;
        }
    }
    return false;
}
#endif

void WalletsCache::ProcessorBase::fundConfidantsWalletsWithFee(const csdb::Amount& totalFee, const cs::ConfidantsKeys& confidants, const std::vector<uint8_t>& realTrusted) {
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

void WalletsCache::ProcessorBase::fundConfidantsWalletsWithExecFee(const csdb::Transaction& transaction, const BlockChain& blockchain) {
    if (!SmartContracts::is_new_state(transaction)) {
        cswarning() << __func__ << ": transaction is not new state";
        return;
    }
    if (isClosedSmart(transaction)) {
        cserror() << "This transaction must be blocked in consensus";
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

double WalletsCache::ProcessorBase::load(const csdb::Transaction& tr, const BlockChain& blockchain) {
    loadTrxForTarget(tr);
    return loadTrxForSource(tr, blockchain);
}

double WalletsCache::ProcessorBase::loadTrxForSource(const csdb::Transaction& tr, const BlockChain& blockchain) {
    csdb::Address wallAddress;

    bool smartIniter = false;
    if (SmartContracts::is_new_state(tr)) {
        wallAddress = findSmartContractIniter(tr, blockchain);
        smartIniter = true;
    }
    else {
        wallAddress = tr.source();
    }

    if (wallAddress == data_.genesisAddress_ || wallAddress == data_.startAddress_)
        return 0;

    WalletId id{};
    if (!findWalletId(wallAddress, id)) {
        cserror() << "Cannot find source wallet, source is " << wallAddress.to_string();
        return 0;
    }
    WalletData& wallData = getWalletData(id, tr.source());

    if (SmartContracts::is_executable(tr)) {
        wallData.balance_ -= csdb::Amount(tr.max_fee().to_double());
        checkClosedSmart(tr);
    }
    else if (SmartContracts::is_new_state(tr)) {
        csdb::Transaction initTransaction = findSmartContractInitTrx(tr, blockchain);
        if (isClosedSmart(initTransaction)) {
            cserror() << "This transaction must be blocked in consensus!";
            wallData.balance_ -= csdb::Amount(initTransaction.max_fee().to_double()) + csdb::Amount(initTransaction.counted_fee().to_double());
        }
        if (SmartContracts::is_executable(initTransaction)) {
            wallData.balance_ += csdb::Amount(initTransaction.max_fee().to_double()) - csdb::Amount(initTransaction.counted_fee().to_double()) -
                                 csdb::Amount(tr.counted_fee().to_double()) - csdb::Amount(tr.user_field(trx_uf::new_state::Fee).value<csdb::Amount>());
        }
        else {
            checkSmartWaitingForMoney(initTransaction, tr);
        }
        //
        WalletId id_s{};
        if (!findWalletId(tr.source(), id_s)) {
            cserror() << "Cannot find source wallet, source is " << wallAddress.to_string();
            return 0;
        }

		WalletData& wallData_s = getWalletData(id_s, tr.source());
		++wallData_s.transNum_;
        wallData_s.trxTail_.push(tr.innerID());

#ifdef MONITOR_NODE              
        wallData_s.lastTransaction_ = tr.id();
#endif
        setModified(id_s);
        //
    }
    else {
        wallData.balance_ -= csdb::Amount(tr.counted_fee().to_double());
    }

    if (!smartIniter) {
        wallData.balance_ -= tr.amount();
		++wallData.transNum_;
        wallData.trxTail_.push(tr.innerID());

#ifdef MONITOR_NODE        
        setWalletTime(wallData.address_, tr.get_time());
#endif

#ifdef TRANSACTIONS_INDEX
        wallData.lastTransaction_ = tr.id();
#endif
    }

    setModified(id);
    return tr.counted_fee().to_double();
}

bool WalletsCache::ProcessorBase::isClosedSmart(const csdb::Transaction& transaction) {
    for (auto& smart : data_.closedSmarts_) {
        if (smart.target() == transaction.target()) {
            return true;
        }
    }
    return false;
}

void WalletsCache::ProcessorBase::checkClosedSmart(const csdb::Transaction& transaction) {
    for (auto it = data_.closedSmarts_.begin(); it != data_.closedSmarts_.end(); ++it) {
        if (it->target() == transaction.target()) {
            data_.closedSmarts_.erase(it);
        }
    }
}

void WalletsCache::ProcessorBase::checkSmartWaitingForMoney(const csdb::Transaction& initTransaction, const csdb::Transaction& newStateTransaction) {
    if (newStateTransaction.user_field(trx_uf::new_state::Value).value<std::string>().empty()) {
        return rollbackReplenishPayableContract(initTransaction, csdb::Amount(newStateTransaction.user_field(trx_uf::new_state::Fee).value<csdb::Amount>()));
    }
    bool waitingSmart = false;
    for (auto it = data_.smartPayableTransactions_.begin(); it != data_.smartPayableTransactions_.end(); it++) {
        if (it->source() == initTransaction.source() && it->innerID() == initTransaction.innerID()) {
            data_.smartPayableTransactions_.erase(it);
            waitingSmart = true;
            break;
        }
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

csdb::Address WalletsCache::findSmartContractIniter(const csdb::Transaction& tr, const BlockChain& blockchain) {
    csdb::Transaction t = findSmartContractInitTrx(tr, blockchain);
    if (t.is_valid()) {
        return t.source();
    }
    return csdb::Address{};
}

csdb::Transaction WalletsCache::findSmartContractInitTrx(const csdb::Transaction& tr, const BlockChain& blockchain) {
    if (!SmartContracts::is_new_state(tr)) {
        cserror() << "Wallets cache: incorrect new_state transaction to get starter";
        return csdb::Transaction{};
    }
    SmartContractRef smartRef(tr.user_field(trx_uf::new_state::RefStart));
    if (!smartRef.is_valid()) {
        cserror() << "Wallets cache: incorrect reference to starter transaction in new_state";
        return csdb::Transaction{};
    }
    csdb::Pool pool = blockchain.loadBlock(smartRef.sequence);
    if (!pool.is_valid()) {
        cserror() << "Wallets cache: failed load block with starter for new_state";
        return csdb::Transaction{};
    }
    auto& transactions = pool.transactions();
    if (transactions.size() > smartRef.transaction) {
        csdb::Transaction trWithIniter = pool.transactions()[smartRef.transaction];
        if (trWithIniter.id() == smartRef.getTransactionID()) {
            return trWithIniter;
        }
    }
    cserror() << "Wallets cache: incorrect starter transaction for new_state";
    return csdb::Transaction{};
}

void WalletsCache::ProcessorBase::loadTrxForTarget(const csdb::Transaction& tr) {
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

#ifdef TRANSACTIONS_INDEX
    wallData.lastTransaction_ = tr.id();
#endif
}

bool WalletsCache::Initer::findWalletId(const csdb::Address& address, WalletId& id) {
    if (!data_.walletsIds_.special().findAnyOrInsertSpecial(address, id))
        return false;
    return true;
}

bool WalletsCache::Updater::findWalletId(const csdb::Address& address, WalletId& id) {
    if (!data_.walletsIds_.normal().find(address, id))
        return false;
    return true;
}

WalletsCache::WalletData& WalletsCache::ProcessorBase::getWalletData(Data& wallets, WalletId id, const csdb::Address& address) {
    id = WalletsIds::Special::makeNormal(id);

    if (id >= wallets.size())
        wallets.resize(id + 1);

    if (!wallets[id]) {
        wallets[id] = new WalletData{};
        convert(address, wallets[id]->address_);
    }

    return *wallets[id];
}

WalletsCache::WalletData& WalletsCache::Initer::getWalletData(WalletId id, const csdb::Address& address) {
    if (WalletsIds::Special::isSpecial(id))
        return ProcessorBase::getWalletData(walletsSpecial_, id, address);
    else
        return ProcessorBase::getWalletData(data_.wallets_, id, address);
}

WalletsCache::WalletData& WalletsCache::Updater::getWalletData(WalletId id, const csdb::Address& address) {
    return ProcessorBase::getWalletData(data_.wallets_, id, address);
}

void WalletsCache::Initer::setModified(WalletId) {
}

void WalletsCache::Updater::setModified(WalletId id) {
    modified_.resize(data_.wallets_.size());
    if (id >= modified_.size()) {
        cserror() << "id >= modified_.size: id=" << id << " modified_.size=" << modified_.size();
        return;
    }
    modified_.set(id);
}

bool WalletsCache::Initer::moveData(WalletId srcIdSpecial, WalletId destIdNormal) {
    if (!WalletsIds::Special::isSpecial(srcIdSpecial))
        return false;
    srcIdSpecial = WalletsIds::Special::makeNormal(srcIdSpecial);

    if (srcIdSpecial >= walletsSpecial_.size())
        return false;
    if (!walletsSpecial_[srcIdSpecial]) {
        cserror() << "Src wallet data should not be empty";
        return false;
    }

    if (destIdNormal >= data_.wallets_.size())
        data_.wallets_.resize(destIdNormal + 1);
    if (data_.wallets_[destIdNormal]) {
        cserror() << "Dest wallet data should be empty";
        //        return false; // examine it
    }
    data_.wallets_[destIdNormal] = walletsSpecial_[srcIdSpecial];
    walletsSpecial_[srcIdSpecial] = nullptr;
    return true;
}

bool WalletsCache::Initer::isFinishedOk() const {
    for (const auto& ptr : walletsSpecial_) {
        if (ptr) {
            cserror() << "Some new wallet was not added to block";
            return false;
        }
    }
    return true;
}

const WalletsCache::WalletData* WalletsCache::Updater::findWallet(WalletId id) const {
    if (id >= data_.wallets_.size())
        return nullptr;
    return data_.wallets_[id];
}

void WalletsCache::iterateOverWallets(const std::function<bool(const WalletData::Address&, const WalletData&)> func) {
    /*for (const auto& wdp : data_) {
      if (!func(wdp.first, wdp.second))
        break;
    }*/

    for (const auto& wdp : wallets_) {
        if (wdp != nullptr && !func(wdp->address_, *wdp))
            break;
    }
}

#ifdef MONITOR_NODE
void WalletsCache::iterateOverWriters(const std::function<bool(const WalletData::Address&, const TrustedData&)> func) {
    for (const auto& wrd : trusted_info_) {
        if (!func(wrd.first, wrd.second))
            break;
    }
}
#endif

/*void WalletsCache::load(csdb::Pool& curr, Mode mode)
{
  PoolHash poolHash;
  convert(curr.hash(), poolHash);
  //csdebug() << __FUNCTION__ << ": mode=" << mode << " poolHash=" << poolHash << " trxNum=" << curr.transactions_count();

  const uint64_t timeStamp = atoll(curr.user_field(0).value<std::string>().c_str());

  auto* walDataPtr = getWalletData(curr.writer_public_key(), timeStamp);
  WalletsCache::WalletData& walWriter = *walDataPtr;
#ifdef MONITOR_NODE
  auto wrWall = curr.writer_public_key();
  auto wrWrIt = writers_.find(curr.writer_public_key());
  if (wrWrIt == writers_.end()) {
    auto res = writers_.insert(std::make_pair(curr.writer_public_key(), TrustedData()));
    wrWrIt = res.first;
  }

  ++wrWrIt->second.times;
#endif

  for (size_t i = 0; i < curr.transactions_count(); i++)
  {
    csdb::Transaction tr = curr.transaction(i);
#ifdef MONITOR_NODE
    wrWrIt->second.totalFee += tr.counted_fee();
#endif
    load(tr, mode, poolHash, walWriter, timeStamp);
  }
}*/

}  // namespace cs
