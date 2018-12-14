#include <base58.h>
#include <csdb/currency.hpp>
#include <lib/system/hash.hpp>
#include <lib/system/keys.hpp>
#include <lib/system/logger.hpp>
#include <lib/system/utils.hpp>
#include <limits>

#include <csnode/blockchain.hpp>
#include <csnode/blockhashes.hpp>
#include <csnode/conveyer.hpp>
#include <csnode/fee.hpp>
#include <solver/smartcontracts.hpp>

using namespace cs;

BlockChain::BlockChain(const std::string& path, csdb::Address genesisAddress, csdb::Address startAddress)
: good_(false)
, dbLock_()
, genesisAddress_(genesisAddress)
, startAddress_(startAddress)
, walletIds_(new WalletsIds)
, walletsCacheStorage_(new WalletsCache(WalletsCache::Config(), genesisAddress, startAddress, *walletIds_))
, walletsPools_(new WalletsPools(genesisAddress, startAddress, *walletIds_))
, cacheMutex_()
, waitersLocker_()
, fee_(std::make_unique<cs::Fee>()) {
  cslog() << "Trying to open DB...";

  if (!storage_.open(path)) {
    cserror() << "Couldn't open database at " << path;
    return;
  }

  cslog() << "DB is opened";

  blockHashes_.reset(new cs::BlockHashes("test_db/dbs.txt"));

  if (storage_.last_hash().is_empty()) {
    cslog() << "Last hash is empty...";
    if (storage_.size()) {
      cslog() << "failed!!! Delete the Database!!! It will be restored from nothing...";
      return;
    }
    walletsCacheUpdater_ = walletsCacheStorage_->createUpdater();
    writeGenesisBlock();
  }
  else {
    cslog() << "Last hash is not empty...";

    {
      std::unique_ptr<WalletsCache::Initer> initer = walletsCacheStorage_->createIniter();
      if (!initFromDB(*initer))
        return;

      if (!initer->isFinishedOk()) {
        cslog() << "Initialization from DB finished with error";
        return;
      }
    }

    walletsCacheUpdater_ = walletsCacheStorage_->createUpdater();
  }

  good_ = true;
}

BlockChain::~BlockChain() {
}

bool BlockChain::isGood() const {
  return good_;
}

bool BlockChain::initFromDB(cs::WalletsCache::Initer& initer) {
  bool res = false;
  try {
    csdb::Pool pool = loadBlock(getLastHash());
    uint32_t last_written_sequence = static_cast<uint32_t>(pool.sequence());
    uint32_t current_sequence = 1;

    while (current_sequence <= last_written_sequence + 1) {
      pool = loadBlock(current_sequence);
      if (!updateWalletIds(pool, initer))
        return false;
      csdb::Pool prev_pool = this->loadBlock(pool.previous_hash());
      const auto& confidants = prev_pool.confidants();
      initer.loadPrevBlock(pool, confidants);
      if (!blockHashes_->initFromPrevBlock(pool))
        return false;

      ++current_sequence;
    }

    blockHashes_->saveDbStructure();

    lastHash_ = getLastHash();

    res = true;
  }
  catch (std::exception& e) {
    cserror() << "Exc=" << e.what();
  }
  catch (...) {
    cserror() << "Exc=...";
  }

  return res;
}

#ifdef TRANSACTIONS_INDEX
void BlockChain::createTransactionsIndex(csdb::Pool& pool) {
  // Update
  std::set<csdb::Address> indexedAddrs;

  auto lbd = [&indexedAddrs, &pool, this](const csdb::Address& addr) {
    if (indexedAddrs.insert(addr).second) {
      csdb::PoolHash lapoo;

      {
        //std::lock_guard<decltype(cacheMutex_)> lock(cacheMutex_);
        //auto wd = walletsCache_->findWallet(addr.public_key());
        //if (wd) lapoo = wd->lastTransaction_.pool_hash();
        lapoo = getLastTransaction(addr).pool_hash();
      }

      storage_.set_previous_transaction_block(addr, pool.hash(), lapoo);
    }
  };

  for (auto& tr : pool.transactions()) {
    lbd(tr.source());
    lbd(tr.target());
  }

  if (pool.transactions().size()) {
    //transactionsCount_ += pool.transactions().size();

    if (lastNonEmptyBlock_.transCount &&
      pool.hash() != lastNonEmptyBlock_.hash)
      previousNonEmpty_[pool.hash()] = lastNonEmptyBlock_;

    lastNonEmptyBlock_.hash = pool.hash();
    lastNonEmptyBlock_.transCount = static_cast<uint32_t>(pool.transactions().size());
  }
}
#endif

uint32_t BlockChain::getLastWrittenSequence() const {
  if (deferredBlock_.is_valid()) {
    return static_cast<uint32_t>(deferredBlock_.sequence());
  }
  else if (blockHashes_->empty()) {
    return static_cast<uint32_t>(-1);
  }
  else {
    return static_cast<uint32_t>(blockHashes_->getDbStructure().last_);
  }
}

void BlockChain::writeGenesisBlock() {
  cswarning() << "Adding the genesis block";

  csdb::Pool genesis;
  csdb::Transaction transaction;
  std::vector<unsigned char> vchRet;

  transaction.set_target(startAddress_);
  transaction.set_source(genesisAddress_);

  transaction.set_currency(csdb::Currency(1));
  transaction.set_amount(csdb::Amount(100'000'000, 0));
  transaction.set_max_fee(csdb::AmountCommission(0.0));
  transaction.set_counted_fee(csdb::AmountCommission(0.0));
  transaction.set_innerID(0);

  genesis.add_transaction(transaction);

  csdb::Address test_address;
  std::string str_addr = "5B3YXqDTcWQFGAqEJQJP3Bg1ZK8FFtHtgCiFLT5VAxpe";
  std::vector<uint8_t> pub_key;
  DecodeBase58(str_addr, pub_key);
  test_address = csdb::Address::from_public_key(pub_key);
  transaction.set_target(test_address);
  transaction.set_source(genesisAddress_);
  transaction.set_currency(csdb::Currency(1));
  transaction.set_amount(csdb::Amount(100'000'000, 0));
  transaction.set_max_fee(csdb::AmountCommission(0.0));
  transaction.set_counted_fee(csdb::AmountCommission(0.0));
  transaction.set_innerID(2);

  genesis.add_transaction(transaction);

  str_addr = "GWe8WZYLBxAqsfPZgejnysXQm5Q697VSsyr3x59RvYBf";
  DecodeBase58(str_addr, pub_key);
  test_address = csdb::Address::from_public_key(pub_key);
  transaction.set_target(test_address);
  transaction.set_source(genesisAddress_);
  transaction.set_currency(csdb::Currency(1));
  transaction.set_amount(csdb::Amount(100'000'000, 0));
  transaction.set_max_fee(csdb::AmountCommission(0.0));
  transaction.set_counted_fee(csdb::AmountCommission(0.0));
  transaction.set_innerID(3);

  genesis.add_transaction(transaction);

  genesis.set_previous_hash(csdb::PoolHash());
  genesis.set_sequence(getLastWrittenSequence() + 1);
  addNewWalletsToPool(genesis);

  cslog() << "Genesis block completed ... trying to save";

  writeBlock(genesis);
  postWriteBlock(genesis);

  //deferredBlock_ = genesis;

  cslog() << genesis.hash().to_string();

  uint32_t bSize;
  genesis.to_byte_stream(bSize);
}


void BlockChain::iterateOverWallets(const std::function<bool(const cs::WalletsCache::WalletData::Address&, const cs::WalletsCache::WalletData&)> func) {
  std::lock_guard<decltype(cacheMutex_)> lock(cacheMutex_);
  walletsCacheStorage_->iterateOverWallets(func);
}

#ifdef MONITOR_NODE
void BlockChain::iterateOverWriters(const std::function<bool(const cs::WalletsCache::WalletData::Address&, const cs::WalletsCache::WriterData&)> func) {
  std::lock_guard<decltype(cacheMutex_)> lock(cacheMutex_);
  walletsCacheStorage_->iterateOverWriters(func);
}

void BlockChain::applyToWallet(const csdb::Address& addr, const std::function<void(const cs::WalletsCache::WalletData&)> func) {
  std::lock_guard<decltype(cacheMutex_)> lock(cacheMutex_);
  //auto wd = walletsCacheStorage_->findWallet(addr.public_key());
  //if (!wd) return;

  WalletId id;
  if (!walletIds_->normal().find(addr, id))
    return;
  auto wd = walletsCacheUpdater_->findWallet(id);

  func(*wd);
}
#endif

csdb::PoolHash BlockChain::getLastHash() const {
  std::lock_guard<decltype(dbLock_)> l(dbLock_);
  if (deferredBlock_.is_valid()) {
    return deferredBlock_.hash();
  }

  return storage_.last_hash();
}

size_t BlockChain::getSize() const {
  std::lock_guard<decltype(dbLock_)> l(dbLock_);
  const auto storageSize = storage_.size();

  return deferredBlock_.is_valid() ? (storageSize + 1) : storageSize;
}

csdb::Pool BlockChain::loadBlock(const csdb::PoolHash& ph) const {
  std::lock_guard<decltype(dbLock_)> l(dbLock_);
  if (deferredBlock_.hash() == ph) {
    return deferredBlock_;
  }
  return storage_.pool_load(ph);
}

csdb::Pool BlockChain::loadBlock(const uint32_t sequence) const {
  std::lock_guard<decltype(dbLock_)> l(dbLock_);
  if (deferredBlock_.sequence() == sequence) {
    return deferredBlock_;
  }
  return storage_.pool_load(sequence);
}

csdb::Pool BlockChain::loadBlockMeta(const csdb::PoolHash& ph, size_t& cnt) const {
  std::lock_guard<decltype(dbLock_)> l(dbLock_);
  return storage_.pool_load_meta(ph, cnt);
}

csdb::Transaction BlockChain::loadTransaction(const csdb::TransactionID& transId) const {
  std::lock_guard<decltype(dbLock_)> l(dbLock_);
  if (deferredBlock_.hash() == transId.pool_hash()) {
    return deferredBlock_.transaction(transId);
  }

  return storage_.transaction(transId);
}

void BlockChain::removeLastBlock() {
  std::lock_guard<decltype(dbLock_)> l(dbLock_);
  deferredBlock_ = csdb::Pool();
//  auto pool = storage_.pool_remove_last();
//  removeWalletsInPoolFromCache(pool);
}

csdb::PoolHash BlockChain::wait_for_block(const csdb::PoolHash& obsolete_block) {
  std::unique_lock<decltype(dbLock_)> l(dbLock_);
  csdb::PoolHash res;

  newBlockCv_.wait(l, [this, &obsolete_block, &res]() {
    res = storage_.last_hash();
    return obsolete_block != res;
  });

  return res;
}

csdb::Address BlockChain::getAddressFromKey(const std::string& key) {
  if (key.size() == PUBLIC_KEY_LENGTH) {
    csdb::Address res = csdb::Address::from_public_key(key.data());
    return res;
  }
  else {
    csdb::internal::WalletId id = *reinterpret_cast<const csdb::internal::WalletId*>(key.data());
    csdb::Address res = csdb::Address::from_wallet_id(id);
    return res;
  }
}

void BlockChain::removeWalletsInPoolFromCache(const csdb::Pool& pool) {
  const auto& new_wallets = pool.newWallets();
  for (const auto& it : new_wallets) {
    walletIds_->normal().remove(csdb::Address::from_wallet_id(it.walletId_));
  }
}

void BlockChain::writeBlock(csdb::Pool& pool) {
  cslog() << "----------------------------- Write Block #" << pool.sequence() << "----------------------------";
  const auto& trusted = pool.confidants();
  cslog() << " trusted count " << trusted.size();
  for(const auto& t : trusted) {
    csdebug() << "\t- " << cs::Utils::byteStreamToHex(t.data(), t.size());
  }
  cslog() << " transactions count " << pool.transactions_count();
  csdebug() << " time: " << pool.user_field(0).value<std::string>().c_str();
  csdebug() << " previous hash: " << lastHash_.to_string();
  csdebug() << " last storage size: " << getSize();

  cslog() << " sequence OK";
  pool.set_previous_hash(lastHash_);

  // former writeBlock():
  {
    std::lock_guard<decltype(dbLock_)> l(dbLock_);
    pool.set_storage(storage_);
  }
  if(!pool.save()) {
    cserror() << "Couldn't save block";
    return;
  }

  {
    std::lock_guard<decltype(waitersLocker_)> l(waitersLocker_);
    newBlockCv_.notify_all();
  }
  cslog() << "--------------------------------------------------------------------------------";
}

void BlockChain::postWriteBlock(csdb::Pool& pool) {
#ifdef TRANSACTIONS_INDEX
  createTransactionsIndex(pool);
#endif

  cslog() << "Block " << pool.sequence() << " saved succesfully";
  if(!updateFromNextBlock(pool)) {
    cserror() << "Couldn't update from next block";
  }

  // inspect transactions against smart contracts, raise special event on every item found:
  if(pool.transactions_count() > 0) {
    size_t idx = 0;
    for(const auto& t : pool.transactions()) {
      if(cs::SmartContracts::is_smart_contract(t)) {
        csdebug() << "BLOCKCHAIN> smart contract trx #" << pool.sequence() << "." << idx;
        emit smartContractEvent_(pool, idx);
      }
      ++idx;
    }
  }
  // end of former writeBlock()

  lastHash_ = pool.hash();
  csdebug() << " last hash: " << lastHash_.to_string();

  recount_trxns(pool);
}

csdb::PoolHash BlockChain::getLastWrittenHash() const {
  return lastHash_;
}

const csdb::Storage& BlockChain::getStorage() const {
  return storage_;
}

csdb::PoolHash BlockChain::getHashBySequence(uint32_t seq) const {
  csdb::PoolHash res{};
  if (deferredBlock_.sequence() == seq) {
    return deferredBlock_.hash();
  }
  if (!blockHashes_->find(seq + 1, res))
    return csdb::PoolHash{};
  return res;
}

uint32_t BlockChain::getRequestedBlockNumber() const {
  return getLastWrittenSequence() + 1;
}

uint64_t BlockChain::getWalletsCount() {
  std::lock_guard<decltype(cacheMutex_)> lock(cacheMutex_);
  return walletsCacheStorage_->getCount();
}

class BlockChain::TransactionsLoader {
public:
  using Transactions = std::vector<csdb::Transaction>;

public:
  TransactionsLoader(csdb::Address wallPubKey, BlockChain::WalletId id, bool isToLoadWalletsPoolsCache, BlockChain& blockchain,
            Transactions& transactions)
  : wallPubKey_(wallPubKey)
  , isToLoadWalletsPoolsCache_(isToLoadWalletsPoolsCache)
  , blockchain_(blockchain)
  , transactions_(transactions) {
    if (isToLoadWalletsPoolsCache_) {
      std::lock_guard<decltype(cacheMutex_)> lock(blockchain_.cacheMutex_);
      blockchain.walletsPools_->addWallet(id);
    }
  }

  bool load(const csdb::PoolHash& poolHash, uint64_t& offset, uint64_t limit, csdb::PoolHash& prevPoolHash) {
    csdb::Pool curr = blockchain_.loadBlock(poolHash);
    if (!curr.is_valid())
      return false;

    if (curr.transactions_count()) {
      bool hasMyTransactions = false;

      for (auto trans : curr.transactions()) {
        if (transactions_.size() == limit)
          break;

        if (trans.target() == wallPubKey_ || trans.source() == wallPubKey_) {
          hasMyTransactions = true;

          if (offset == 0)
            transactions_.push_back(trans);
          else
            --offset;
        }
      }

      if (hasMyTransactions && isToLoadWalletsPoolsCache_) {
        std::lock_guard<decltype(cacheMutex_)> lock(blockchain_.cacheMutex_);
        blockchain_.walletsPools_->loadPrevBlock(curr);
      }
    }

    prevPoolHash = curr.previous_hash();

    return true;
  }

private:
  csdb::Address wallPubKey_;
  const bool isToLoadWalletsPoolsCache_;
  BlockChain& blockchain_;
  Transactions& transactions_;
};

void BlockChain::getTransactions(Transactions& transactions, csdb::Address address, uint64_t offset, uint64_t limit) {
  if (offset >= getSize())
    return;
  if (!limit)
    return;
  WalletId id{};
  csdb::Address wallPubKey;
  WalletsPools::WalletData::PoolsHashes hashesArray;

  if (!findDataForTransactions(address, wallPubKey, id, hashesArray))
    return;

  getTransactions(transactions, wallPubKey, id, hashesArray, offset, limit);
}

bool BlockChain::findDataForTransactions(csdb::Address address, csdb::Address& wallPubKey, WalletId& id,
                                         WalletsPools::WalletData::PoolsHashes& hashesArray) const {
  std::lock_guard<decltype(cacheMutex_)> lock(cacheMutex_);

  if (address.is_wallet_id()) {
    id = address.wallet_id();

    const WalletData* wallDataPtr = walletsCacheUpdater_->findWallet(id);
    if (!wallDataPtr)
      return false;
    WalletsCache::convert(wallDataPtr->address_, wallPubKey);
  }
  else  // if (address.is_public_key())
  {
    if (!walletIds_->normal().find(address, id))
      return false;
    wallPubKey = address;
  }

  const WalletsPools::WalletData* wallData = walletsPools_->findWallet(id);
  if (wallData)
    hashesArray = wallData->poolsHashes_;
  return true;
}

void BlockChain::getTransactions(Transactions& transactions, csdb::Address wallPubKey, WalletId id,
                                 const WalletsPools::WalletData::PoolsHashes& hashesArray, uint64_t offset,
                                 uint64_t limit) {
  bool isToLoadWalletsPoolsCache = hashesArray.empty() && wallPubKey != genesisAddress_ && wallPubKey != startAddress_;
  if (wallPubKey.is_public_key()) {
    WalletId _id;
    if (!findWalletId(wallPubKey, _id))
      return;
    wallPubKey = csdb::Address::from_wallet_id(_id);
  }
  TransactionsLoader trxLoader(wallPubKey, id, isToLoadWalletsPoolsCache, *this, transactions);

  csdb::PoolHash prevHash = getLastHash();

  for (size_t i = hashesArray.size() - 1; i != std::numeric_limits<decltype(i)>::max(); --i) {
    const auto& poolHashData = hashesArray[i];

    if (poolHashData.trxNum < WalletsPools::WalletData::PoolHashData::maxTrxNum && poolHashData.trxNum <= offset) {
      offset -= poolHashData.trxNum;
      continue;
    }

    csdb::PoolHash currHash;
    WalletsPools::convert(poolHashData.poolHash, currHash);

    if (!trxLoader.load(currHash, offset, limit, prevHash))
      return;
    if (transactions.size() >= limit)
      return;
  }

  while (true) {
    csdb::PoolHash currHash = prevHash;
    if (!trxLoader.load(currHash, offset, limit, prevHash))
      break;
  }
}

template <typename WalletCacheProcessor>
bool BlockChain::updateWalletIds(const csdb::Pool& pool, WalletCacheProcessor& proc) {
  try {
    std::lock_guard<decltype(cacheMutex_)> lock(cacheMutex_);

    const csdb::Pool::NewWallets& newWallets = pool.newWallets();
    for (const auto& newWall : newWallets) {
      csdb::Address newWallAddress;
      if (!pool.getWalletAddress(newWall, newWallAddress)) {
        cserror() << "Wrong new wallet data";
        return false;
      }

      if (!insertNewWalletId(newWallAddress, newWall.walletId_, proc)) {
        cserror() << "Wallet was already added as new";
        return false;
      }
    }
  }
  catch (std::exception& e) {
    cserror() << "Exc=" << e.what();
    return false;
  }
  catch (...) {
    cserror() << "Exc=...";
    return false;
  }
  return true;
}

bool BlockChain::insertNewWalletId(const csdb::Address& newWallAddress, WalletId newWalletId,
                                   WalletsCache::Initer& initer) {
  WalletId idSpecial{};

  if (!walletIds_->special().insertNormal(newWallAddress, newWalletId, idSpecial)) {
    cserror() << "Cannot add new wallet";
    return false;
  }

  if (WalletsIds::Special::isSpecial(idSpecial)) {
    if (!initer.moveData(idSpecial, newWalletId)) {
      cserror() << "Cannot move special wallet id data to newWalletId: idSpecial=" << idSpecial
                << " newWalletId=" << newWalletId;
      return false;
    }
  }

  return true;
}

bool BlockChain::insertNewWalletId(const csdb::Address& newWallAddress, WalletId newWalletId, WalletsCache::Updater&) {
  if (!walletIds_->normal().insert(newWallAddress, newWalletId)) {
    cserror() << "Cannot add new wallet";
    return false;
  }

  return true;
}

void BlockChain::addNewWalletToPool(const csdb::Address& walletAddress,
                                    const csdb::Pool::NewWalletInfo::AddressId& addressId,
                                    csdb::Pool::NewWallets& newWallets) {
  if (!walletAddress.is_public_key())
    return;

  if (walletAddress == genesisAddress_)
    return;

  WalletId id{};

  if (getWalletId(walletAddress, id)) {
    newWallets.emplace_back(csdb::Pool::NewWalletInfo{addressId, id});
  }
}

void BlockChain::addNewWalletsToPool(csdb::Pool& pool) {
  csdb::Pool::NewWallets* newWallets = pool.newWallets();

  if (!newWallets) {
    cserror() << "Pool is read-only";
    return;
  }

  newWallets->clear();

  csdb::Pool::Transactions& transactions = pool.transactions();

  for (size_t idx = 0; idx < transactions.size(); ++idx) {
    {
      csdb::Pool::NewWalletInfo::AddressId addressId = {idx, csdb::Pool::NewWalletInfo::AddressType::AddressIsSource};
      addNewWalletToPool(transactions[idx].source(), addressId, *newWallets);
    }
    {
      csdb::Pool::NewWalletInfo::AddressId addressId = {idx, csdb::Pool::NewWalletInfo::AddressType::AddressIsTarget};
      addNewWalletToPool(transactions[idx].target(), addressId, *newWallets);
    }
  }

  const auto& confidants = pool.confidants();
  size_t confWalletsIndexStart = transactions.size();
  for (size_t i = 0; i < confidants.size(); ++i) {
    csdb::Pool::NewWalletInfo::AddressId addressId = { confWalletsIndexStart + i,
                                                      csdb::Pool::NewWalletInfo::AddressType::AddressIsTarget };
    addNewWalletToPool(csdb::Address::from_public_key(confidants[i]), addressId, *newWallets);
  }
}

bool BlockChain::updateFromNextBlock(csdb::Pool& nextPool) {
  if (!walletsCacheUpdater_) {
    cserror() << "!walletsCacheUpdater";
    return false;
  }

  try {
    std::lock_guard<decltype(cacheMutex_)> lock(cacheMutex_);
    csdb::Pool pool = this->loadBlock(nextPool.previous_hash());
    const auto& currentRoundConfidants = pool.confidants();
    walletsCacheUpdater_->loadNextBlock(nextPool, currentRoundConfidants);
    walletsPools_->loadNextBlock(nextPool);

    blockHashes_->loadNextBlock(nextPool);
    if (!blockHashes_->saveDbStructure()) {
      cslog() << "Error writing DB structure";
    }
  }
  catch (std::exception& e) {
    cserror() << "Exc=" << e.what();
    return false;
  }
  catch (...) {
    cserror() << "Exc=...";
    return false;
  }
  return true;
}

bool BlockChain::findWalletData(const csdb::Address& address, WalletData& wallData, WalletId& id) const {
  if (address.is_wallet_id()) {
    id = address.wallet_id();
    return findWalletData(address.wallet_id(), wallData);
  }

  std::lock_guard<decltype(cacheMutex_)> lock(cacheMutex_);

  if (!walletIds_->normal().find(address, id))
    return false;

  return findWalletData_Unsafe(id, wallData);
}

bool BlockChain::findWalletData(WalletId id, WalletData& wallData) const {
  std::lock_guard<decltype(cacheMutex_)> lock(cacheMutex_);
  return findWalletData_Unsafe(id, wallData);
}

bool BlockChain::findWalletData_Unsafe(WalletId id, WalletData& wallData) const {
  const WalletData* wallDataPtr = walletsCacheUpdater_->findWallet(id);
  if (wallDataPtr) {
    wallData = *wallDataPtr;
    return true;
  }
  return false;
}

bool BlockChain::getModifiedWallets(Mask& dest) const {
  std::lock_guard<decltype(cacheMutex_)> lock(cacheMutex_);

  bool isNewModified =
      (walletsCacheUpdater_->getModified().size() != dest.size()) || walletsCacheUpdater_->getModified().any();

  dest.resize(walletsCacheUpdater_->getModified().size(), true);
  dest |= walletsCacheUpdater_->getModified();
  return isNewModified;
}

bool BlockChain::findWalletId(const WalletAddress& address, WalletId& id) const {
  if (address.is_wallet_id()) {
    id = address.wallet_id();
    return true;
  }
  else if (address.is_public_key()) {
    std::lock_guard<decltype(cacheMutex_)> lock(cacheMutex_);
    return walletIds_->normal().find(address, id);
  }

  cserror() << "Wrong address";
  return false;
}

bool BlockChain::getWalletId(const WalletAddress& address, WalletId& id) {
  if (address.is_wallet_id()) {
    id = address.wallet_id();
    return false;
  }
  else if (address.is_public_key()) {
    std::lock_guard<decltype(cacheMutex_)> lock(cacheMutex_);
    return walletIds_->normal().get(address, id);
  }

  cserror() << "Wrong address";
  return false;
}

bool BlockChain::findAddrByWalletId(const WalletId id, csdb::Address& addr) const {
  if (!walletIds_->normal().findaddr(id, addr))
    return false;
  return true;
}

void BlockChain::recount_trxns(const std::optional<csdb::Pool>& new_pool) {
  if (new_pool.value().transactions_count()) {
    csdb::Address addr_send, addr_recv;
    for (const auto& trx : new_pool.value().transactions()) {
      addr_send = trx.source();
      if (addr_send.is_wallet_id()) {
        WalletId id = *reinterpret_cast<const WalletId*>(addr_send.to_api_addr().data());
        if (!findAddrByWalletId(id, addr_send))
          return;
      }
      addr_recv = trx.target();
      if (addr_recv.is_wallet_id()) {
        WalletId id = *reinterpret_cast<const WalletId*>(addr_recv.to_api_addr().data());
        if (!findAddrByWalletId(id, addr_recv))
          return;
      }
      transactionsCount_[addr_send].sendCount++;
      transactionsCount_[addr_recv].recvCount++;
    }

#ifdef TRANSACTIONS_INDEX
    total_transactions_count_+= new_pool.value().transactions().size();
#endif
  }
}

const BlockChain::AddrTrnxCount& BlockChain::get_trxns_count(const csdb::Address& addr) {
  return transactionsCount_[addr];
}

std::pair<bool, std::optional<csdb::Pool>> BlockChain::recordBlock(csdb::Pool pool, bool requireAddWallets) {
  const auto last_seq = getLastWrittenSequence();
  const auto pool_seq = pool.sequence();
  csdebug() << "BLOCKCHAIN> finish & store block #" << pool_seq << " to chain";

  if (last_seq == pool_seq) {
    updateLastBlockTrustedConfidants(pool);
    return std::make_pair(true, deferredBlock_);
  }

  if (last_seq + 1 != pool_seq) {
    cserror() << "BLOCKCHAIN> cannot record block #" << pool_seq << " to chain, last sequence " << last_seq;
    return std::make_pair(false, std::nullopt);
  }

  fee_->CountFeesInPool(*this, &pool);
  pool.set_previous_hash(getLastWrittenHash());

  if (requireAddWallets) {
    csdebug() << "BLOCKCHAIN> record block #" << pool_seq << " to chain, add new wallets to pool";
    addNewWalletsToPool(pool);
  }
  else {
    // ready-to-record block does not require anything
    csdebug() << "BLOCKCHAIN> record block #" << pool_seq << " to chain, update wallets ids";
    updateWalletIds(pool, *walletsCacheUpdater_);
  }

  if (deferredBlock_.is_valid()) {
    writeBlock(deferredBlock_);

    const auto seq = std::max(deferredBlock_.sequence() - 1, static_cast<uint64_t>(1));
    csdb::PoolHash hash = getHashBySequence(seq);
    auto loadPool = loadBlock(hash);
    if (!loadPool.is_valid()) {
      cserror() << "BLOCKCHAIN> test load pool failed: #" << seq << ", getHashBySequence " << hash.to_string()
        << ", error " << storage_.last_error() << " (" << storage_.last_error_message() << ")";
    }
    else {
      cslog() << "BLOCKCHAIN> test load pool ok: #" << seq;
    }
  }

  if(!pool.compose()) {
    cserror() << __func__ << " Couldn't compose block";
  }

  deferredBlock_ = pool;

  postWriteBlock(deferredBlock_);

  return std::make_pair(true, deferredBlock_);
}

bool BlockChain::storeBlock(csdb::Pool pool, bool by_sync) {
  const auto last_seq = getLastWrittenSequence();
  const auto pool_seq = pool.sequence();
  if (pool_seq <= last_seq) {
    // ignore
    csdebug() << "BLOCKCHAIN> ignore oudated block #" << pool_seq << ", last written #" << last_seq;
    // it is not error, so caller code nothing to do with it
    return true;
  }
  if (pool_seq == last_seq + 1) {
    // write immediately
    if (recordBlock(pool, !by_sync).first) {
      csdebug() << "BLOCKCHAIN> block #" << pool_seq << " has recorded to chain successfully";
      // unable to call because stack overflow in case of huge written blocks amount possible:
      //testCachedBlocks();
      return true;
    }
    removeWalletsInPoolFromCache(pool);
    csdebug() << "BLOCKCHAIN> failed to store block #" << pool_seq << " to chain";
    return false;
  }
  if (cachedBlocks_.count(pool_seq) > 0) {
    csdebug() << "BLOCKCHAIN> ignore duplicated block #" << pool_seq << " in cache";
    // it is not error, so caller code nothing to do with it
    return true;
  }
  // cache block for future recording
  cachedBlocks_.emplace(pool_seq, BlockMeta{std::move(pool), by_sync});
  csdebug() << "BLOCKCHAIN> cache block #" << pool_seq << " for future (" << cachedBlocks_.size() << " total)";
  // cache always successful
  return true;
}

std::optional<csdb::Pool> BlockChain::createBlock(csdb::Pool pool) {
  const auto last_seq = getLastWrittenSequence();
  const auto pool_seq = pool.sequence();
  if (pool_seq != last_seq + 1) {
    return std::nullopt;
  }
  return recordBlock(pool, true).second;
}

void BlockChain::testCachedBlocks() {
  csdebug() << "BLOCKCHAIN> test cached blocks";
  if (cachedBlocks_.empty()) {
    csdebug() << "BLOCKCHAIN> no cached blocks";
  }
  // retrieve blocks until cache empty or block sequence is interrupted
  while (!cachedBlocks_.empty()) {
    size_t desired_seq = getLastWrittenSequence() + 1;
    const auto oldest = cachedBlocks_.cbegin();
    if (oldest->first < desired_seq) {
      // clear outdated block and select next one:
      csdebug() << "BLOCKCHAIN> remove outdated block #" << oldest->first << " from cache";
      cachedBlocks_.erase(oldest);
    }
    else if (oldest->first == desired_seq) {
      csdebug() << "BLOCKCHAIN> retrieve required block #" << desired_seq << " from cache";
      // retrieve and use block if it is exactly what we need:
      auto& data = cachedBlocks_.at(desired_seq);
      bool ok = storeBlock(data.pool, data.by_sync);
      if (!ok) {
        cserror() << "BLOCKCHAIN> failed to record cached block to chain, drop it & wait to request again";
      }
      cachedBlocks_.erase(desired_seq);
    }
    else {
      // stop processing, we have not got required block in cache yet
      break;
    }
  }
}

std::size_t BlockChain::getCachedBlocksSize() const {
  return cachedBlocks_.size();
}

std::vector<BlockChain::SequenceInterval> BlockChain::getRequiredBlocks() const
{
  const auto firstSequence = getLastWrittenSequence() + 1;
  const auto currentRoundNumber = cs::Conveyer::instance().currentRoundNumber();
  const auto roundNumber = currentRoundNumber ? std::max(firstSequence, currentRoundNumber - 1) : 0;

  // return at least [next, 0] or [next, currentRoundNumber]:
  std::vector<SequenceInterval> vec { std::make_pair(firstSequence, roundNumber) };

  // always point to last interval
  auto firstUpper = cachedBlocks_.upper_bound(firstSequence);

  if (firstUpper != cachedBlocks_.end()) {
    auto sequence = firstUpper->first;
    vec[0].second = sequence - 1;

    while ((++firstUpper) != cachedBlocks_.end()) {
      ++sequence;
      if (firstUpper->first != sequence) {
        vec.emplace_back(std::make_pair(sequence, firstUpper->first - 1));
        sequence = firstUpper->first;
      }
    }
  }

  // add last interval [final + 1, end]
  if (!cachedBlocks_.empty()) {
    const auto lastCahedBlock = cachedBlocks_.crbegin()->first;
    if (roundNumber > lastCahedBlock) {
      vec.emplace_back(std::make_pair(lastCahedBlock, roundNumber));
    }
  }

  return vec;
}

void BlockChain::updateLastBlockTrustedConfidants(const csdb::Pool& pool) {
  const auto& deffConfidants = deferredBlock_.confidants();
  const auto& newConfidants = pool.confidants();

  if (deffConfidants != newConfidants) {
    cswarning() << "BLOCKCHAIN> " << " confidants in new pool is same as cached pool";
    return;
  }

  csdebug() << "BLOCKCHAIN> " << __func__;

  deferredBlock_.set_confidants(newConfidants);
  deferredBlock_.update_binary();
}

void BlockChain::setTransactionsFees(TransactionsPacket& packet)
{
  if(!fee_) {
    return;
  }
  if(packet.transactionsCount() == 0) {
    return;
  }
  fee_->CountFeesInPool(*this, &packet);
}

#ifdef TRANSACTIONS_INDEX
csdb::TransactionID BlockChain::getLastTransaction(const csdb::Address& addr) {
  /*std::lock_guard<decltype(cacheMutex_)> lock(cacheMutex_);
  auto wd = walletsCache_->findWallet(addr.public_key());
  if (!wd) return csdb::TransactionID();
  return wd->lastTransaction_;*/

  //std::lock_guard<decltype(cacheMutex_)> lock(cacheMutex_);
  BlockChain::Transactions transactions;
  getTransactions(transactions, addr, static_cast<uint64_t>(0), static_cast<uint64_t>(1));//last trx
  if(transactions.size())
    return transactions[0].id();
  return csdb::TransactionID();
}

csdb::PoolHash BlockChain::getPreviousPoolHash(const csdb::Address& addr, const csdb::PoolHash& ph) {
  std::lock_guard<decltype(dbLock_)> lock(dbLock_);
  return storage_.get_previous_transaction_block(addr, ph);
}

std::pair<csdb::PoolHash, uint32_t> BlockChain::getLastNonEmptyBlock() {
  std::lock_guard<decltype(dbLock_)> lock(dbLock_);
  return std::make_pair(lastNonEmptyBlock_.hash, lastNonEmptyBlock_.transCount);
}

std::pair<csdb::PoolHash, uint32_t> BlockChain::getPreviousNonEmptyBlock(const csdb::PoolHash& ph) {
  std::lock_guard<decltype(dbLock_)> lock(dbLock_);
  const auto it = previousNonEmpty_.find(ph);
  if (it != previousNonEmpty_.end())
    return std::make_pair(it->second.hash, it->second.transCount);

  return std::pair<csdb::PoolHash, uint32_t>();
}

TransactionsIterator::TransactionsIterator(BlockChain& bc, const csdb::Address& addr) : bc_(bc), addr_(addr) {
  setFromTransId(bc_.getLastTransaction(addr));
}

void TransactionsIterator::setFromTransId(const csdb::TransactionID& lTrans) {
  if (lTrans.is_valid()) {
    lapoo_ = bc_.loadBlock(lTrans.pool_hash());
    it_ = lapoo_.transactions().rbegin() + (lapoo_.transactions().size() - lTrans.index() - 1);
  }
  else lapoo_ = csdb::Pool{};
}

bool TransactionsIterator::isValid() const {
  return lapoo_.is_valid();
}

void TransactionsIterator::next() {
  while (++it_ != lapoo_.transactions().rend()) {
    if (it_->source() == addr_ || it_->target() == addr_)
      break;
  }

  // Oops, no more in this blockfTransactionsListGet
  if (it_ == lapoo_.transactions().rend()) {
    auto ph = bc_.getPreviousPoolHash(addr_, lapoo_.hash());
    lapoo_ = bc_.loadBlock(ph);

    if (lapoo_.is_valid()) {
      it_ = lapoo_.transactions().rbegin();
      // transactions() cannot be empty
      if (it_->source() != addr_ && it_->target() != addr_)
        next();  // next should be executed only once
    }
  }
}

#else

void TransactionsIterator::setFromHash(const csdb::PoolHash& ph) {
  auto hash = ph;
  bool found = false;

  while (!found) {
    lapoo_ = bc_.loadBlock(hash);
    if (!lapoo_.is_valid()) break;

    for (it_ = lapoo_.transactions().rbegin();
      it_ != lapoo_.transactions().rend();
      ++it_) {
      if (it_->source() == addr_ || it_->target() == addr_) {
        found = true;
        break;
      }
    }

    hash = lapoo_.previous_hash();
  }
}

TransactionsIterator::TransactionsIterator(BlockChain& bc,
  const csdb::Address& addr) : bc_(bc),
  addr_(addr) {
  setFromHash(bc_.getLastHash());
}

bool TransactionsIterator::isValid() const {
  return lapoo_.is_valid();
}

void TransactionsIterator::next() {
  bool found = false;

  while (++it_ != lapoo_.transactions().rend()) {
    if (it_->source() == addr_ || it_->target() == addr_) {
      found = true;
      break;
    }
  }

  if (!found) setFromHash(lapoo_.previous_hash());
}

#endif
