#include <csdb/amount_commission.hpp>
#include <algorithm>
#include <csnode/walletscache.hpp>
#include <csnode/walletsids.hpp>
#include <lib/system/logger.hpp>
#include <solver/smartcontracts.hpp>
#include <blockchain.hpp>

using namespace std;

namespace cs {
void WalletsCache::convert(const csdb::Address& address, WalletData::Address& walletAddress) {
  walletAddress = address.public_key();
}

void WalletsCache::convert(const WalletData::Address& walletAddress, csdb::Address& address) {
  address = csdb::Address::from_public_key(walletAddress);
}

WalletsCache::WalletsCache(const Config& config, csdb::Address genesisAddress, csdb::Address startAddress,
                           WalletsIds& walletsIds)
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

// ProcessorBase
void WalletsCache::ProcessorBase::load(csdb::Pool& pool, const cs::ConfidantsKeys& confidants, const BlockChain& blockchain) {
  const csdb::Pool::Transactions& transactions = pool.transactions();
  double totalAmountOfCountedFee = 0;
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
  for (const auto &it : confidants) {
    std::copy(it.begin(), it.end(), addr_trusted.begin());
    auto it_trusted = data_.trusted_info_.find(addr_trusted);
    if (it_trusted == data_.trusted_info_.end()) {
      const auto res = data_.trusted_info_.insert(std::make_pair(addr_trusted, TrustedData()));
      it_trusted = res.first;
    }
    ++it_trusted->second.times_trusted;
  }
#endif

  for (auto itTrx = transactions.crbegin(); itTrx != transactions.crend(); ++itTrx) {
    totalAmountOfCountedFee += load(*itTrx, blockchain);
  }
#ifdef MONITOR_NODE
  it_writer->second.totalFee += totalAmountOfCountedFee;
#endif

  if (totalAmountOfCountedFee > 0) {
    fundConfidantsWalletsWithFee(totalAmountOfCountedFee, confidants);
  }

  auto timeStamp = atoll(pool.user_field(0).value<std::string>().c_str());
#ifdef MONITOR_NODE
  setWalletTime(wrWall, timeStamp);
#endif
}
#ifdef MONITOR_NODE
bool WalletsCache::ProcessorBase::setWalletTime(const WalletData::Address& address, const uint64_t &p_timeStamp) {
  for (auto &it : data_.wallets_) {
    if (it != nullptr && it->address_ == address) {
      it->createTime_ = p_timeStamp;
      return true;
    }
  }
  return false;
}
#endif

void WalletsCache::ProcessorBase::fundConfidantsWalletsWithFee(double totalFee, const cs::ConfidantsKeys& confidants) {
  if (!confidants.size()) {
    cslog() << "WALLETS CACHE>> NO CONFIDANTS";
    return;
  }
  double feeToEachConfidant = totalFee / confidants.size();
  for (size_t i = 0; i < confidants.size(); ++i) {
    WalletId confidantId{};
    csdb::Address confidantAddress = csdb::Address::from_public_key(confidants[i]);
    if (!findWalletId(confidantAddress, confidantId)) {
      cserror() << "Cannot find confidant wallet, source is " << confidantAddress.to_string();
      return;
    }
    WalletData& walletData = getWalletData(confidantId, confidantAddress);
    walletData.balance_ += feeToEachConfidant;
    setModified(confidantId);
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
  } else {
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
  } else if (SmartContracts::is_new_state(tr)) {
    csdb::Transaction initTransaction = findSmartContractInitTrx(tr, blockchain);
    wallData.balance_ += csdb::Amount(initTransaction.max_fee().to_double())
      - csdb::Amount(initTransaction.counted_fee().to_double())
      - csdb::Amount(tr.counted_fee().to_double());
  } else {
    wallData.balance_ -= csdb::Amount(tr.counted_fee().to_double());
  }


  if (!smartIniter) {
  wallData.balance_ -= tr.amount();
  wallData.trxTail_.push(tr.innerID());

#ifdef MONITOR_NODE
  ++wallData.transNum_;
  setWalletTime(wallData.address_, tr.get_time());
#endif

#ifdef TRANSACTIONS_INDEX
  wallData.lastTransaction_ = tr.id();
#endif
  }

  setModified(id);
  return tr.counted_fee().to_double();
}

csdb::Address WalletsCache::findSmartContractIniter(const csdb::Transaction& tr, const BlockChain& blockchain) {
  SmartContractRef smartRef;
  smartRef.from_user_field(tr.user_field(trx_uf::new_state::RefStart));
  csdb::Pool pool = blockchain.loadBlock(smartRef.sequence);
  csdb::Transaction trWithIniter = pool.transactions()[smartRef.transaction];
  return trWithIniter.source();
}

csdb::Transaction WalletsCache::findSmartContractInitTrx(const csdb::Transaction& tr, const BlockChain& blockchain) {
  SmartContractRef smartRef;
  smartRef.from_user_field(tr.user_field(trx_uf::new_state::RefStart));
  csdb::Pool pool = blockchain.loadBlock(smartRef.sequence);
  csdb::Transaction trWithIniter = pool.transactions()[smartRef.transaction];
  return trWithIniter;
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

#ifdef MONITOR_NODE
  if (tr.source() != tr.target())  // Already counted in loadTrxForSource
  ++wallData.transNum_;

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

WalletsCache::WalletData& WalletsCache::ProcessorBase::getWalletData(Data& wallets, WalletId id,
                                                                     const csdb::Address& address) {
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
