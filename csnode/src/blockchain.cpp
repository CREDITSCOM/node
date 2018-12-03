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

using namespace cs;

BlockChain::BlockChain(const std::string& path, csdb::Address genesisAddress, csdb::Address startAddress)
: good_(false)
, dbLock_()
, waitersLocker_()
, cacheMutex_()
, globalSequence_(static_cast<decltype(globalSequence_)>(-1))
, blockRequestIsNeeded_(false)
, genesisAddress_(genesisAddress)
, startAddress_(startAddress)
, walletIds_(new WalletsIds)
, walletsCacheStorage_(new WalletsCache(WalletsCache::Config(), genesisAddress, startAddress, *walletIds_))
, walletsPools_(new WalletsPools(genesisAddress, startAddress, *walletIds_))
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

  std::fill(emptySignature_.begin(), emptySignature_.end(), 0u);
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

  if (!updateFromNextBlock(pool)) {
    cserror() << "Couldn't update from next block";
  }

  {
    std::lock_guard<decltype(waitersLocker_)> l(waitersLocker_);
    newBlockCv_.notify_all();
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

  globalSequence_ = genesis.sequence();
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

void BlockChain::removeLastBlock() {
  std::lock_guard<decltype(dbLock_)> l(dbLock_);
  auto pool = storage_.pool_remove_last();
  removeWalletsInPoolFromCache(pool);
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
  cslog() << "----------------------------- Write Block #" << pool.sequence() << "----------------------------";
  cslog() << " transactions count " << pool.transactions_count();
  csdebug() << " time: " << pool.user_field(0).value<std::string>().c_str();
  csdebug() << " previous hash: " << lastHash_.to_string();
  csdebug() << " last storage size: " << storage_.size();

  bool result = false;
  if (pool.sequence() == getLastWrittenSequence() + 1) {
    cslog() << " sequence OK";

    pool.set_previous_hash(lastHash_);
    writeBlock(pool);
    lastHash_ = pool.hash();
    csdebug() << " last hash: " << lastHash_.to_string();
    csdebug() << " signature: " << cs::Utils::byteStreamToHex(pool.signature().data(), 32);

    if (globalSequence_ == getLastWrittenSequence()) {
      blockRequestIsNeeded_ = false;
    }
    result = true;
  }
  else {
    cslog() << " sequence failed, chain syncro start";
    ////////////////////////////////////////////////////////////////////////////////////////////// Syncro!!!
    globalSequence_ = pool.sequence();
    blockRequestIsNeeded_ = true;
    result = false;
  }
  cslog() << "--------------------------------------------------------------------------------";
  return result;
}

csdb::PoolHash BlockChain::getLastWrittenHash() const {
  return lastHash_;
}

const csdb::Storage& BlockChain::getStorage() const {
  return storage_;
}

csdb::Pool::sequence_t BlockChain::getGlobalSequence() const {
  return globalSequence_;
}

void BlockChain::setGlobalSequence(uint32_t seq) {
  globalSequence_ = seq;
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
  return blockRequestIsNeeded_;
}

class BlockChain::TransactionsLoader {
public:
  using Transactions = std::vector<csdb::Transaction>;

public:
  TransactionsLoader(csdb::Address wallPubKey, BlockChain::WalletId id, bool isToLoadWalletsPoolsCache, BlockChain& blockchain,
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
      transactionsCount_[addr_send].recvCount++;
    }
  }
}

const BlockChain::AddrTrnxCount& BlockChain::get_trxns_count(const csdb::Address& addr) {
  return transactionsCount_[addr];
}

std::pair<bool, std::optional<csdb::Pool>> BlockChain::recordBlock(csdb::Pool pool,
                                                                   std::optional<cs::Signature> writer_signature,
                                                                   std::optional<cs::PrivateKey> writer_key) {
  const auto last_seq = getLastWrittenSequence();
  const auto pool_seq = pool.sequence();
  csdebug() << "BLOCKCHAIN> finish & store block #" << pool_seq << " to chain";
  if (last_seq + 1 != pool_seq) {
    cserror() << "BLOCKCHAIN> cannot record block #" << pool_seq << " to chain, last sequence " << last_seq;
    return std::make_pair(false, std::nullopt);
  }

  fee_->CountFeesInPool(*this, &pool);
  pool.set_previous_hash(getLastWrittenHash());

  if (writer_key.has_value()) {
    csdebug() << "BLOCKCHAIN> signing block #" << pool_seq;
    addNewWalletsToPool(pool);
    //pool.sign(writer_key.value());
  }

  if (writer_signature.has_value()) {
    csdebug() << "BLOCKCHAIN> record block #" << pool_seq << " to chain, add new wallets to pool";
    if (writer_key.has_value()) {
      cswarning() << "BLOCKCHAIN> pool have just been signed. R u sure u want to test passed signature?";
    }

    addNewWalletsToPool(pool);

    // new block require finishing
    const auto& sig = writer_signature.value();
    //if (pool.verify_signature(std::string(sig.begin(), sig.end()))) {
      csdebug() << "BLOCKCHAIN> writer signature is verified, record block to chain";
      putBlock(pool);
   // }
   // else {
   //   cswarning() << "BLOCKCHAIN> writer signature verification failed, drop block out";
   //   removeWalletsInPoolFromCache(pool);
      // caller code may handle the situation
    //  return std::make_pair(false, pool);
   // }
  }
  else {
    // ready-to-record block does not require anything
    csdebug() << "BLOCKCHAIN> record block #" << pool_seq
              << " to chain, no further verificaton need, update wallets ids";

    updateWalletIds(pool, *walletsCacheUpdater_);
    putBlock(pool);
  }
  setGlobalSequence(getLastWrittenSequence());
  return std::make_pair(true, pool);
}

bool BlockChain::storeBlock(csdb::Pool pool, std::optional<cs::Signature> writer_signature /*= {}*/) {
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
    if (recordBlock(pool, writer_signature, std::nullopt).first) {
      csdebug() << "BLOCKCHAIN> block #" << pool_seq << " has recorded to chain successfully";
      // unable to call because stack overflow in case of huge written blocks amount possible:
      //testCachedBlocks();
      return true;
    }
    csdebug() << "BLOCKCHAIN> failed to store block #" << pool_seq << " to chain";
    return false;
  }
  if (cachedBlocks_.count(pool_seq) > 0) {
    csdebug() << "BLOCKCHAIN> ignore duplicated block #" << pool_seq << " in cache";
    // it is not error, so caller code nothing to do with it
    return true;
  }
  // cache block for future recording
  cs::PublicKey key;
  const auto& pool_key = pool.writer_public_key();
  if (pool_key.size() == key.size()) {
    std::copy(pool_key.data(), pool_key.data() + pool_key.size(), key.begin());
  }
  else {
    cserror() << "BLOCKCHAIN> cached block does not contain writer key";
    std::fill(key.begin(), key.end(), 0);
  }
  cachedBlocks_.emplace(pool_seq,
                        cs::PoolSyncMeta{std::move(pool), writer_signature.value_or(emptySignature_), std::move(key)});
  csdebug() << "BLOCKCHAIN> cache block #" << pool_seq << " for future (" << cachedBlocks_.size() << " total)";
  // cache always successful
  return true;
}

std::optional<csdb::Pool> BlockChain::createBlock(csdb::Pool pool, const cs::PrivateKey& writer_key) {
  const auto last_seq = getLastWrittenSequence();
  const auto pool_seq = pool.sequence();
  if (pool_seq != last_seq + 1) {
    return std::nullopt;
  }
  return recordBlock(pool, std::nullopt, writer_key).second;
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
      bool ok = false;
      if (std::equal(data.signature.cbegin(), data.signature.cend(), emptySignature_.cbegin())) {
        ok =  storeBlock(data.pool);
      }
      else {
        ok = storeBlock(data.pool, data.signature);
      }
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
  const auto roundNumber = currentRoundNumber ? currentRoundNumber - 1 : 0;

  // return at least [next..0]:
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
