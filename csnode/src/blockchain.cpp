#include <base58.h>
#include <csdb/currency.h>
#include <lib/system/hash.hpp>
#include <lib/system/keys.hpp>
#include <lib/system/logger.hpp>
#include <lib/system/utils.hpp>
#include <limits>

#include <csnode/blockhashes.hpp>
#include <csnode/blockchain.hpp>

using namespace cs;

BlockChain::BlockChain(const std::string& path, csdb::Address genesisAddress, csdb::Address startAddress)
: good_(false)
, global_sequence(static_cast<decltype(global_sequence)>(-1))
, blockRequestIsNeeded(false)
, genesisAddress_(genesisAddress)
, startAddress_(startAddress)
, walletIds_(new WalletsIds)
, walletsCacheStorage_(new WalletsCache(WalletsCache::Config(), genesisAddress, startAddress, *walletIds_))
, walletsPools_(new WalletsPools(genesisAddress, startAddress, *walletIds_)) {
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

    if (!writeGenesisBlock()) {
      cslog() << "failed!!! Cannot write genesis block";
      return;
    }
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
      initer.loadPrevBlock(pool);
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

bool BlockChain::writeNewBlock(csdb::Pool& pool) {
  csdebug() << "writeNewBlock is running";
  return putBlock(pool);
}

void BlockChain::writeBlock(csdb::Pool& pool) {
  {
    std::lock_guard<decltype(dbLock_)> l(dbLock_);
    pool.set_storage(storage_);
  }

  if (!pool.compose())
    if (!pool.compose()) {
      cserror() << "Couldn't compose block";
      if (!pool.save())
        return;
    }

  if (!pool.save()) {
    cserror() << "Couldn't save block";
    return;
  }

  cslog() << "Block " << pool.sequence() << " saved succesfully";

  if (!updateFromNextBlock(pool))
    cserror() << "Couldn't update from next block";

  {
    std::lock_guard<decltype(waiters_locker)> l(waiters_locker);
    new_block_cv.notify_all();
  }
}

uint32_t BlockChain::getLastWrittenSequence() const {
  return (blockHashes_->empty()) ? static_cast<uint32_t>(-1)
                                 : static_cast<uint32_t>(blockHashes_->getDbStructure().last_);
}

bool BlockChain::writeGenesisBlock() {
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
  finishNewBlock(genesis);

  cslog() << "Genesis block completed ... trying to save";

  if (!writeNewBlock(genesis))
    return false;

  global_sequence = genesis.sequence();
  cslog() << genesis.hash().to_string();

  uint32_t bSize;
  genesis.to_byte_stream(bSize);

  return true;
}

csdb::PoolHash BlockChain::getLastHash() const {
  std::lock_guard<decltype(dbLock_)> l(dbLock_);
  return storage_.last_hash();
}

size_t BlockChain::getSize() const {
  std::lock_guard<decltype(dbLock_)> l(dbLock_);
  return storage_.size();
}

csdb::Pool BlockChain::loadBlock(const csdb::PoolHash& ph) const {
  std::lock_guard<decltype(dbLock_)> l(dbLock_);
  return storage_.pool_load(ph);
}

csdb::Pool BlockChain::loadBlock(const uint32_t sequence) const {
  std::lock_guard<decltype(dbLock_)> l(dbLock_);
  return storage_.pool_load(sequence);
}

csdb::Pool BlockChain::loadBlockMeta(const csdb::PoolHash& ph, size_t& cnt) const {
  std::lock_guard<decltype(dbLock_)> l(dbLock_);
  return storage_.pool_load_meta(ph, cnt);
}

csdb::Transaction BlockChain::loadTransaction(const csdb::TransactionID& transId) const {
  std::lock_guard<decltype(dbLock_)> l(dbLock_);
  return storage_.transaction(transId);
}

csdb::PoolHash BlockChain::wait_for_block(const csdb::PoolHash& obsolete_block) {
  std::unique_lock<decltype(dbLock_)> l(dbLock_);
  csdb::PoolHash res;

  new_block_cv.wait(l, [this, &obsolete_block, &res]() {
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

bool BlockChain::finishNewBlock(csdb::Pool& pool) {
  pool.set_sequence(getLastWrittenSequence() + 1);
  addNewWalletsToPool(pool);
  return true;
}

void BlockChain::removeWalletsInPoolFromCache(const csdb::Pool& pool) {
  const auto& new_wallets = pool.newWallets();
  for (const auto& it : new_wallets) {
    walletIds_->normal().remove(csdb::Address::from_wallet_id(it.walletId_));
  }
}

bool BlockChain::onBlockReceived(csdb::Pool& pool) {
  csdebug() << "onBlockReceived is running";

  if (!updateWalletIds(pool, *walletsCacheUpdater_)) {
    cserror() << "Couldn't update wallet ids";
  }

  return putBlock(pool);
}

bool BlockChain::putBlock(csdb::Pool& pool) {
  // Put on top
  cslog() << "----------------------------- Write New Block ------------------------------";
  cslog() << " sequence " << pool.sequence() << ", transactions count " << pool.transactions_count();

  csdebug() << " time: " << pool.user_field(0).value<std::string>().c_str();
  csdebug() << " last hash: " << lastHash_.to_string();
  csdebug() << " last storage size: " << storage_.size();

  bool result = false;
  if (pool.sequence() == getLastWrittenSequence() + 1) {
    cslog() << " sequence OK";

    pool.set_previous_hash(lastHash_);
    writeBlock(pool);
    lastHash_ = pool.hash();
    if (global_sequence == getLastWrittenSequence()) {
      blockRequestIsNeeded = false;
    }
    result = true;
  }
  else {
    cslog() << " sequence failed, chain syncro start";
    ////////////////////////////////////////////////////////////////////////////////////////////// Syncro!!!
    global_sequence = pool.sequence();
    blockRequestIsNeeded = true;
    result = false;
  }
  cslog() << "----------------------------------------------------------------------------";
  return result;
}

csdb::PoolHash BlockChain::getLastWrittenHash() const {
  return lastHash_;
}

const csdb::Storage& BlockChain::getStorage() const {
  return storage_;
}

csdb::Pool::sequence_t BlockChain::getGlobalSequence() const {
  return global_sequence;
}

void BlockChain::setGlobalSequence(uint32_t seq) {
  global_sequence = seq;
}

csdb::PoolHash BlockChain::getHashBySequence(uint32_t seq) const {
  csdb::PoolHash res{};
  if (!blockHashes_->find(seq, res))
    return csdb::PoolHash{};
  return res;
}

uint32_t BlockChain::getRequestedBlockNumber() const {
  return getLastWrittenSequence() + 1;
}

bool BlockChain::getBlockRequestNeed() const {
  return blockRequestIsNeeded;
}

class BlockChain::TrxLoader {
public:
  using Transactions = std::vector<csdb::Transaction>;

public:
  TrxLoader(csdb::Address wallPubKey, BlockChain::WalletId id, bool isToLoadWalletsPoolsCache, BlockChain& blockchain,
            Transactions& transactions)
  : wallPubKey_(wallPubKey)
  , id_(id)
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
  BlockChain::WalletId id_;
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
  TrxLoader trxLoader(wallPubKey, id, isToLoadWalletsPoolsCache, *this, transactions);

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

  if (pool.sequence() != 0) {
    csdb::Pool::NewWalletInfo::AddressId addressId = {transactions.size(),
                                                      csdb::Pool::NewWalletInfo::AddressType::AddressIsTarget};
    addNewWalletToPool(csdb::Address::from_public_key(pool.writer_public_key()), addressId, *newWallets);
  }
}

bool BlockChain::updateFromNextBlock(csdb::Pool& nextPool) {
  if (!walletsCacheUpdater_) {
    cserror() << "!walletsCacheUpdater";
    return false;
  }

  try {
    std::lock_guard<decltype(cacheMutex_)> lock(cacheMutex_);

    walletsCacheUpdater_->loadNextBlock(nextPool);
    walletsPools_->loadNextBlock(nextPool);

    blockHashes_->loadNextBlock(nextPool);
    if (!blockHashes_->saveDbStructure())
      cslog() << "Error writing DB structure";
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
      m_trxns_count[addr_send].sendCount++;
      m_trxns_count[addr_send].recvCount++;
    }
  }
}

const BlockChain::AddrTrnxCount& BlockChain::get_trxns_count(const csdb::Address& addr) {
  return m_trxns_count[addr];
}
