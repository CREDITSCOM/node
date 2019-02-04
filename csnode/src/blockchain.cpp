#include <base58.h>
#include <csdb/currency.hpp>
#include <lib/system/hash.hpp>
#include <lib/system/logger.hpp>
#include <lib/system/utils.hpp>
#include <limits>

#include <csnode/blockchain.hpp>
#include <csnode/blockhashes.hpp>
#include <csnode/conveyer.hpp>
#include <csnode/datastream.hpp>
#include <csnode/fee.hpp>
#include <solver/smartcontracts.hpp>

#include <client/config.hpp>

//#define RECREATE_INDEX

// uncomment this to generate new cheat db file (__integr.seq) every time it is absent in BD directory
//#define RECREATE_CHEAT

using namespace cs;

void generateCheatDbFile(std::string, const BlockHashes&);
bool validateCheatDbFile(std::string, const BlockHashes&);

BlockChain::BlockChain(csdb::Address genesisAddress, csdb::Address startAddress)
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
}

BlockChain::~BlockChain() {
}

bool BlockChain::init(const std::string& path)
{
  cslog() << "Trying to open DB...";

  size_t total_loaded = 0;
  csdb::Storage::OpenCallback progress = [&](const csdb::Storage::OpenProgress& progress) {
    ++total_loaded;
    if(progress.poolsProcessed % 1000 == 0) {
      std::cout << '\r' << progress.poolsProcessed << "";
    }
    return false;
  };
  if(!storage_.open(path, progress)) {
    cserror() << "Couldn't open database at " << path;
    return false;
  }

  cslog() << "\rDB is opened, loaded " << total_loaded << " blocks";

  blockHashes_ = std::make_unique<cs::BlockHashes>();

  if(storage_.last_hash().is_empty()) {
    csdebug() << "Last hash is empty...";
    if(storage_.size()) {
      cserror() << "failed!!! Delete the Database!!! It will be restored from nothing...";
      return false;
    }
    walletsCacheUpdater_ = walletsCacheStorage_->createUpdater();
    writeGenesisBlock();
    generateCheatDbFile(path, *blockHashes_);
  }
  else {
    csdebug() << "Last hash is not empty. Reading wallets";
    {
      std::unique_ptr<WalletsCache::Initer> initer = walletsCacheStorage_->createIniter();
      if(!initFromDB(*initer))
        return false;

      if(!initer->isFinishedOk()) {
        cserror() << "Initialization from DB finished with error";
        return false;
      }
    }

    walletsCacheUpdater_ = walletsCacheStorage_->createUpdater();

    if(!validateCheatDbFile(path, *blockHashes_)) {
      cserror() << "Bad database version";
      return false;
    }
    fee_->ResetTrustedCache(*this);
    std::cout << "Done\n";
  }

#if defined(TRANSACTIONS_INDEX) && defined(RECREATE_INDEX)
  for (uint32_t seq = 0; seq <= getLastSequence(); ++seq) {
    auto pool = loadBlock(seq);
    createTransactionsIndex(pool);
  }

  cslog() << "Recreated the index 0->" << getLastSequence() << ". Finishing with error now. Because we can";
  return false;
#else
  good_ = true;
  return true;
#endif
}

bool BlockChain::isGood() const {
  return good_;
}

bool BlockChain::initFromDB(cs::WalletsCache::Initer& initer) {
  bool res = false;
  try {
    csdb::Pool pool = loadBlock(getLastHash());
    const cs::Sequence last_written_sequence = pool.sequence();
    cs::Sequence current_sequence = 0;

    size_t cnt = 1;
    std::cout << "Reading wallets...\n";
    while (current_sequence <= last_written_sequence) {
      pool = loadBlock(current_sequence);
      if (!updateWalletIds(pool, initer)) {
        return false;
      }
      // currently confidants are stored in related pool, not in previous one:
      //csdb::Pool prev_pool = this->loadBlock(pool.previous_hash());
      const auto& confidants = pool.confidants();
      initer.loadPrevBlock(pool, confidants);

      blockHashes_->initFromPrevBlock(pool);

      ++current_sequence;
#ifdef TRANSACTIONS_INDEX
      if(pool.transactions().size()) {
        total_transactions_count_ += pool.transactions().size();

        if(lastNonEmptyBlock_.transCount && pool.hash() != lastNonEmptyBlock_.hash)
          previousNonEmpty_[pool.hash()] = lastNonEmptyBlock_;

        lastNonEmptyBlock_.hash = pool.hash();
        lastNonEmptyBlock_.transCount = static_cast<uint32_t>(pool.transactions().size());
      }
#endif
      if(cnt % 1000 == 0) {
        std::cout << '\r' << cnt;
      }
      ++cnt;
    }
    std::cout << "\rDone, handled " << cnt - 1 << " blocks\n";

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
#ifdef RECREATE_INDEX
  static std::map<csdb::Address, csdb::PoolHash> lapoos;
#endif

  // Update
  std::set<csdb::Address> indexedAddrs;

  auto lbd = [&indexedAddrs, &pool, this](const csdb::Address& addr) {
    auto key = get_addr_by_type(addr, ADDR_TYPE::PUBLIC_KEY);
    if (indexedAddrs.insert(key).second) {
#ifdef RECREATE_INDEX
      csdb::PoolHash lapoo = lapoos[key];
      lapoos[key] = pool.hash();
#else
      csdb::PoolHash lapoo = getLastTransaction(key).pool_hash();
#endif
      storage_.set_previous_transaction_block(key, pool.hash(), lapoo);
    }
  };

  for (auto& tr : pool.transactions()) {
    lbd(tr.source());
    lbd(tr.target());
  }

  if (pool.transactions().size()) {
    total_transactions_count_ += pool.transactions().size();

    if (lastNonEmptyBlock_.transCount && pool.hash() != lastNonEmptyBlock_.hash)
      previousNonEmpty_[pool.hash()] = lastNonEmptyBlock_;

    lastNonEmptyBlock_.hash = pool.hash();
    lastNonEmptyBlock_.transCount = static_cast<uint32_t>(pool.transactions().size());
  }
}
#endif

cs::Sequence BlockChain::getLastSequence() const {
  std::lock_guard<decltype(dbLock_)> l(dbLock_);

  if (deferredBlock_.is_valid()) {
    return deferredBlock_.sequence();
  }
  else if (!blockHashes_->empty()) {
    return blockHashes_->getDbStructure().last_;
  }
  else {
    return static_cast<cs::Sequence> (-1);
  }
}

const std::string CHEAT_FILENAME = "__integr.seq";

std::string prepareCheatData(std::string& path, const BlockHashes& bh) {
  if (path.size() && path.back() != '/') {
    path.push_back('/');
  }

  path += CHEAT_FILENAME;

  csdb::PoolHash genHash = bh.find(0);
  cs::Bytes hb = genHash.to_binary();

  cs::DataStream stream(hb);
  stream << NODE_VERSION;

  return EncodeBase58(hb);
}

void generateCheatDbFile(std::string path, const BlockHashes& bh) {
  const auto cd = prepareCheatData(path, bh);

  std::ofstream f(path);
  f << cd;
}

bool validateCheatDbFile(std::string path, const BlockHashes& bh) {
#if defined(RECREATE_CHEAT)
  std::string origin = path;
#endif// RECREATE_CHEAT
  const auto cd = prepareCheatData(path, bh);

  std::ifstream f(path);
#if defined(RECREATE_CHEAT)
  if(!f) {
    generateCheatDbFile(origin, bh);
    cswarning() << "Blockchain: cannot open special mark so it was regenerated";
    return true;
  }
#endif// RECREATE_CHEAT
  std::string rcd;
  f >> rcd;

  return rcd == cd;
}

void BlockChain::writeGenesisBlock() {
  cswarning() << "Adding the genesis block";

  csdb::Pool genesis;
  csdb::Transaction transaction;

  std::string str_addr = "5B3YXqDTcWQFGAqEJQJP3Bg1ZK8FFtHtgCiFLT5VAxpe";
  std::vector<uint8_t> pub_key;
  DecodeBase58(str_addr, pub_key);
  csdb::Address test_address = csdb::Address::from_public_key(pub_key);
  transaction.set_target(test_address);
  transaction.set_source(genesisAddress_);
  transaction.set_currency(csdb::Currency(1));
  transaction.set_amount(csdb::Amount(249'471'071, 0));
  transaction.set_max_fee(csdb::AmountCommission(0.0));
  transaction.set_counted_fee(csdb::AmountCommission(0.0));
  transaction.set_innerID(0);

  genesis.add_transaction(transaction);

  genesis.set_previous_hash(csdb::PoolHash());
  genesis.set_sequence(getLastSequence() + 1);
  addNewWalletsToPool(genesis);

  csdebug() << "Genesis block completed ... trying to save";

  finalizeBlock(genesis, true);
  deferredBlock_ = genesis;
  emit storeBlockEvent_(deferredBlock_);

  csdebug() << genesis.hash().to_string();

  uint32_t bSize;
  genesis.to_byte_stream(bSize);
}


void BlockChain::iterateOverWallets(const std::function<bool(const cs::WalletsCache::WalletData::Address&, const cs::WalletsCache::WalletData&)> func) {
  std::lock_guard<decltype(cacheMutex_)> lock(cacheMutex_);
  walletsCacheStorage_->iterateOverWallets(func);
}

#ifdef MONITOR_NODE
void BlockChain::iterateOverWriters(const std::function<bool(const cs::WalletsCache::WalletData::Address&, const cs::WalletsCache::TrustedData&)> func) {
  std::lock_guard<decltype(cacheMutex_)> lock(cacheMutex_);
  walletsCacheStorage_->iterateOverWriters(func);
}

void BlockChain::applyToWallet(const csdb::Address& addr, const std::function<void(const cs::WalletsCache::WalletData&)> func) {
  std::lock_guard<decltype(cacheMutex_)> lock(cacheMutex_);
  WalletId id;
  if (!walletIds_->normal().find(addr, id))
    return;
  auto wd = walletsCacheUpdater_->findWallet(id);

  func(*wd);
}
#endif

csdb::PoolHash BlockChain::getLastHash() const {
  std::lock_guard<decltype(dbLock_)> l(dbLock_);

  if (deferredBlock_.is_valid())
    return deferredBlock_.hash().clone();

  return storage_.last_hash();
}

size_t BlockChain::getSize() const {
  std::lock_guard<decltype(dbLock_)> l(dbLock_);

  const auto storageSize = storage_.size();
  return deferredBlock_.is_valid() ? (storageSize + 1) : storageSize;
}

csdb::Pool BlockChain::loadBlock(const csdb::PoolHash& ph) const {
  if(ph.is_empty()) {
    return csdb::Pool {};
  }

  std::lock_guard<decltype(dbLock_)> l(dbLock_);

  if(deferredBlock_.hash() == ph)
    return deferredBlock_.clone();

  return storage_.pool_load(ph);
}

csdb::Pool BlockChain::loadBlock(const cs::Sequence sequence) const {

  std::lock_guard<decltype(dbLock_)> l(dbLock_);

  if (deferredBlock_.is_valid() && deferredBlock_.sequence() == sequence) {
    // deferredBlock already composed:
    return deferredBlock_.clone();
  }
  // storage loads blocks by 1-based index: 1 => pool[0], 2 => pool[1] etc.
  return storage_.pool_load(sequence + 1);
}

csdb::Pool BlockChain::loadBlockMeta(const csdb::PoolHash& ph, size_t& cnt) const {
  std::lock_guard<decltype(dbLock_)> l(dbLock_);

  if(deferredBlock_.hash() == ph)
    return deferredBlock_.clone();

  return storage_.pool_load_meta(ph, cnt);
}

csdb::Transaction BlockChain::loadTransaction(const csdb::TransactionID& transId) const {
  std::lock_guard<decltype(dbLock_)> l(dbLock_);

  if (deferredBlock_.hash() == transId.pool_hash())
    return deferredBlock_.transaction(transId).clone();

  return storage_.transaction(transId);
}

void BlockChain::removeLastBlock() {
  csmeta(csdebug) << "begin";
  csdb::Pool pool {};

  std::lock_guard<decltype(dbLock_)> l(dbLock_);

  if (deferredBlock_.is_valid()) {
    pool = deferredBlock_;
    deferredBlock_ = csdb::Pool {};
  }
  else {
    pool = storage_.pool_remove_last();
  }

  if (!pool.is_valid()) {
    csmeta(cserror) << "Error! Removed pool is not valid";
    return;
  }

  const auto lastHash = blockHashes_->getLast();
  const csdb::PoolHash poolHash = pool.hash();

  if (lastHash == poolHash) {
    blockHashes_->removeLast();
    csmeta(csdebug) << "Remove last hash is ok, sequence: " << pool.sequence();
  }
  else {
    csmeta(cserror) << "Error! Last pool hash mismatch";
    const auto findSequence = blockHashes_->find(poolHash);
    const auto& bh = blockHashes_->getHashes();
    csmeta(cserror) << "Block hashes size: " << bh.size()
                    << ", Pool sequence: " << pool.sequence()
                    << ", in Block hashes sequence: " << findSequence;
    if (findSequence == 0) {
      for (std::size_t i = 0; i < bh.size(); ++i) {
        csmeta(csdebug) << "Block hash [" << i << "]: " << bh[i].to_string();
      }
    }
  }

#ifdef TRANSACTIONS_INDEX
  total_transactions_count_ -= pool.transactions().size();
#endif

  removeWalletsInPoolFromCache(pool);

  cachedBlocks_.erase(cachedBlocks_.lower_bound(pool.sequence()), cachedBlocks_.end());

  emit removeBlockEvent(pool.sequence());

  csmeta(csdebug) << "done";
}

csdb::PoolHash BlockChain::wait_for_block(const csdb::PoolHash& obsolete_block) {
  csunused(obsolete_block);
  std::unique_lock<decltype(dbLock_)> l(dbLock_);
  csdb::PoolHash res;

  newBlockCv_.wait(l);
  /*newBlockCv_.wait(l, [this, &obsolete_block, &res]() {
    res = storage_.last_hash();
    //return obsolete_block != res;
    return obsolete_block == res;
  });*/
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

void BlockChain::logBlockInfo(csdb::Pool& pool)
{
  const auto& trusted = pool.confidants();
  std::string  realTrustedString;
  for (auto& i : pool.realTrusted()) {
    realTrustedString = realTrustedString + "[" + std::to_string(static_cast<int>(i)) + "] ";
  }

  csdebug() << " trusted count " << trusted.size() << ", RealTrusted = " << realTrustedString;
  for(const auto& t : trusted) {
    csdebug() << "\t- " << cs::Utils::byteStreamToHex(t.data(), t.size());
  }
  csdebug() << " transactions count " << pool.transactions_count();
  if(pool.user_field_ids().count(0) > 0) {
    csdebug() << " time: " << pool.user_field(0).value<std::string>().c_str();
  }
  csdebug() << " previous hash: " << pool.previous_hash().to_string();
  csdebug() << " hash: " << pool.hash().to_string();
  csdebug() << " last storage size: " << getSize();
}

bool BlockChain::finalizeBlock(csdb::Pool& pool, bool isTrusted) {
  if(!pool.compose()) {
    csmeta(cserror) << "Couldn't compose block: " << pool.sequence();
    return false;
  }

  const auto& confidants = pool.confidants();
  const auto& signatures = pool.signatures();
  if (signatures.empty() && !isTrusted && pool.sequence() != 0) {
    csdebug() << "The  pool #" << pool.sequence() << " doesn't contain signatures";
    return false;
  }

  if (signatures.size() < confidants.size() / 2U + 1U && !isTrusted && pool.sequence() != 0) {
    csdebug() << "The number of signatures is insufficient";
    return false;
  }

  size_t truePoolSignatures = 0;

  for (auto& it : signatures) {
    if (it.first < confidants.size()) {
      if (cscrypto::verifySignature(it.second, confidants.at(it.first), pool.hash().to_binary().data(), pool.hash().to_binary().size())) {
        csdebug() << "The signature of " << cs::Utils::byteStreamToHex(confidants.at(it.first).data(), confidants.at(it.first).size()) << " is valid";
        ++truePoolSignatures;
      }
      else {
        csdebug() << "The signature of " << cs::Utils::byteStreamToHex(confidants.at(it.first).data(), confidants.at(it.first).size()) << " is NOT VALID";
      }
    }
    else {
      cswarning() << "The number of confidants in pool doesn't correspond the indexes of the signatures";
      return false;
    }
  }
  if (truePoolSignatures == signatures.size()) {
    cslog() << "The number of signatures is sufficient ant all of them are OK!";
  }
  else {
    cswarning() << "Some of Pool Signatures aren't valid. The pool will not be written to DB";
    return false;
  }

#ifdef TRANSACTIONS_INDEX
  createTransactionsIndex(pool);
#endif

  if(!updateFromNextBlock(pool)) {
    csmeta(cserror) << "Error in updateFromNextBlock()";
    return false;
  }

  csmeta(csdetails) << "last hash: " << pool.hash().to_string();
  recount_trxns(pool);
  return true;
}

const csdb::Storage& BlockChain::getStorage() const {
  return storage_;
}

csdb::PoolHash BlockChain::getHashBySequence(cs::Sequence seq) const {
  std::lock_guard<decltype(dbLock_)> l(dbLock_);

  if (deferredBlock_.sequence() == seq)
    return deferredBlock_.hash().clone();

  return blockHashes_->find(seq);
}

cs::Sequence BlockChain::getRequestedBlockNumber() const {
  return getLastSequence() + 1;
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
  /*if (offset >= getSize())
    return;
  if (!limit)
    return;
  WalletId id{};
  csdb::Address wallPubKey;
  WalletsPools::WalletData::PoolsHashes hashesArray;

  if (!findDataForTransactions(address, wallPubKey, id, hashesArray))
    return;

  getTransactions(transactions, wallPubKey, id, hashesArray, offset, limit);*/
  for (auto trIt = TransactionsIterator(*this, address);
    trIt.isValid();
    trIt.next()) {
    if (offset > 0) {
      --offset;
      continue;
    }

    transactions.push_back(*trIt);
    //transactions.back().set_time(trIt.getPool().get_time());

    if (--limit == 0) break;
  }
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
    // currently block stores own round confidants, not next round:
    const auto& currentRoundConfidants = nextPool.confidants();
    walletsCacheUpdater_->loadNextBlock(nextPool, currentRoundConfidants);
    walletsPools_->loadNextBlock(nextPool);

    if (!blockHashes_->loadNextBlock(nextPool)) {
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
      addr_send = get_addr_by_type(trx.source(), ADDR_TYPE::PUBLIC_KEY);
      addr_recv = get_addr_by_type(trx.target(), ADDR_TYPE::PUBLIC_KEY);
      transactionsCount_[addr_send].sendCount++;
      transactionsCount_[addr_recv].recvCount++;
    }
//#ifdef TRANSACTIONS_INDEX
    //total_transactions_count_+= new_pool.value().transactions().size();
//#endif
  }
}

const BlockChain::AddrTrnxCount& BlockChain::get_trxns_count(const csdb::Address& addr) {
  return transactionsCount_[addr];
}

std::pair<bool, std::optional<csdb::Pool>> BlockChain::recordBlock(csdb::Pool pool, bool requireAddWallets, bool isTrusted)
{
  const auto last_seq = getLastSequence();
  const auto pool_seq = pool.sequence();
  csdebug() << "BLOCKCHAIN> finish & store block #" << pool_seq << " to chain";
  int signs =0;
  if(last_seq + 1 != pool_seq) {
    cserror() << "BLOCKCHAIN> cannot record block #" << pool_seq << " to chain, last sequence " << last_seq;
    return std::make_pair(false, std::nullopt);
  }

  fee_->CountFeesInPool(*this, &pool);
  pool.set_previous_hash(getLastHash());

  if(requireAddWallets) {
    csdebug() << "BLOCKCHAIN> record block #" << pool_seq << " to chain, add new wallets to pool";
    addNewWalletsToPool(pool);
  }
  else {
    // ready-to-record block does not require anything
    csdebug() << "BLOCKCHAIN> record block #" << pool_seq << " to chain, update wallets ids";
    updateWalletIds(pool, *walletsCacheUpdater_);
  }

  constexpr cs::Sequence NoSequence = std::numeric_limits<cs::Sequence>::max();
  cs::Sequence flushed_block_seq = NoSequence;

  {
    std::lock_guard<decltype(dbLock_)> l(dbLock_);
    signs = deferredBlock_.signatures().size();
    if (!deferredBlock_.recompose()) {
      csdebug() << "The block binary representation updated successfully";
    }
    else {
      csdebug() << "The block binary representation update failed";
    }
    if(deferredBlock_.is_valid()) {
      deferredBlock_.set_storage(storage_);
      if(deferredBlock_.save()) {
        flushed_block_seq = deferredBlock_.sequence();
        std::lock_guard<decltype(waitersLocker_)> l2(waitersLocker_);
        newBlockCv_.notify_all();
      }
      else {
        csmeta(cserror) << "Couldn't save block: " << deferredBlock_.sequence();
      }
    }
  }

  if(flushed_block_seq != NoSequence) {

    csdebug() << "---------------------------- Flush block #" << flushed_block_seq << " to disk ---------------------------";
    csdebug() << "signatures amount = " << signs << " see block info above";
    csdebug() << "----------------------------------------------------------------------------------";

    emit writeBlockEvent(flushed_block_seq);
  }

  {
    std::lock_guard<decltype(dbLock_)> l(dbLock_);

    // next 2 calls order is extremely significant: finalizeBlock() may call to smarts-"enqueue"-"execute", so deferredBlock MUST BE SET properly
    deferredBlock_ = pool;
    if(finalizeBlock(deferredBlock_, isTrusted)) {
      csdebug() << "The block is correct";
    } 
    else {
      csdebug() << "the signatures of the block are incorrect";
      return std::make_pair(false, csdb::Pool{});
    }
    pool = deferredBlock_.clone();
  }

  emit storeBlockEvent_(pool);

  // log cached block
  csdebug() << "----------------------- Defer block #" << pool.sequence() << " until next round ----------------------";
  logBlockInfo(pool);
  csdebug() << "----------------------------------- " << pool.sequence() << " --------------------------------------";

  return std::make_pair(true, pool);
}

bool BlockChain::addSignaturesToDeferredBlock(std::vector<std::pair<uint8_t, cscrypto::Signature>> blockSignatures) {
  //{
  //  std::lock_guard<decltype(dbLock_)> l(dbLock_);
    if (deferredBlock_.is_valid()) {
      for(auto& it : blockSignatures) {
        deferredBlock_.add_signature(it.first,it.second);
      }
      if(deferredBlock_.signatures().size() == blockSignatures.size()) {
      }
      return true;
    }
 /* }*/
  cserror() << __func__ << " ... Failed!!!";
  return false;
}

bool BlockChain::storeBlock(csdb::Pool pool, bool by_sync) {
  csdebug() << __func__ << ":";
  const auto last_seq = getLastSequence();
  const auto pool_seq = pool.sequence();
  if (pool_seq <= last_seq) {
    // ignore
    csdebug() << "BLOCKCHAIN> ignore oudated block #" << pool_seq << ", last written #" << last_seq;
    // it is not error, so caller code nothing to do with it
    return true;
  }
 
  if (pool_seq == last_seq) {
    if (!deferredBlock_.signatures().empty()) {
      // ignore
      csdebug() << "BLOCKCHAIN> ignore oudated block #" << pool_seq << ", last written #" << last_seq;
      // it is not error, so caller code nothing to do with it
      return true;
    }
    else {
      csdebug() << "BLOCKCHAIN> we have to rewrite #" << pool_seq ;
      removeLastBlock();
    }
  }

  if (pool_seq == last_seq + 1) {
    if (pool.previous_hash() != getLastHash()) {
      csdebug() << "BLOCKCHAIN> new pool\'s prev. hash does not equal to current last hash, remove own last block and cancel store operation";
      if(getLastHash().is_empty()) {
        cswarning() << "BLOCKCHAIN> own last hash is empty";
      }
      if(pool.previous_hash().is_empty()) {
        cswarning() << "BLOCKCHAIN> new pool\'s prev. hash is empty";
      }
      removeLastBlock();
      return false;
    }
    // write immediately
    if (recordBlock(pool, !by_sync, false).first) {
      csdebug() << "BLOCKCHAIN> block #" << pool_seq << " has recorded to chain successfully";
      // unable to call because stack overflow in case of huge written blocks amount possible:
      //testCachedBlocks();
      return true;
    }
    else {
      removeLastBlock();
      return false;
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
  csdebug() << "BLOCKCHAIN> cached block has " << pool.signatures().size();
  cachedBlocks_.emplace(pool_seq, BlockMeta{std::move(pool), by_sync});
  csdebug() << "BLOCKCHAIN> cache block #" << pool_seq << " for future (" << cachedBlocks_.size() << " total)";
  // cache always successful
  return true;
}

std::optional<csdb::Pool> BlockChain::createBlock(csdb::Pool pool) {
  const auto last_seq = getLastSequence();
  const auto pool_seq = pool.sequence();
  if (pool_seq != last_seq + 1) {
    return std::nullopt;
  }
  return recordBlock(pool, true, true).second;
}

void BlockChain::testCachedBlocks() {
  csdebug() << "BLOCKCHAIN> test cached blocks";
  if (cachedBlocks_.empty()) {
    csdebug() << "BLOCKCHAIN> no cached blocks";
  }
  // retrieve blocks until cache empty or block sequence is interrupted
  while (!cachedBlocks_.empty()) {
    size_t desired_seq = getLastSequence() + 1;
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

/*cs::Bytes BlockChain::getKeyFromAddress(csdb::Address& addr) const {
  if (!addr.is_public_key())
    findAddrByWalletId(addr.wallet_id(), addr);

  return addr.public_key();
}*/

std::size_t BlockChain::getCachedBlocksSize() const {
  return cachedBlocks_.size();
}

std::vector<BlockChain::SequenceInterval> BlockChain::getRequiredBlocks() const
{
  const auto firstSequence = getLastSequence() + 1;
  const auto currentRoundNumber = cs::Conveyer::instance().currentRoundNumber();

  if (firstSequence >= currentRoundNumber) {
    return std::vector<SequenceInterval>();
  }

  const auto roundNumber = currentRoundNumber > 0 ? std::max(firstSequence, currentRoundNumber - 1) : 0;

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

void BlockChain::setTransactionsFees(TransactionsPacket& packet)
{
  if(!fee_ || packet.transactionsCount() == 0)
    return;
  fee_->CountFeesInPool(*this, &packet);
}

csdb::Address BlockChain::get_addr_by_type(const csdb::Address &addr, ADDR_TYPE type) const {
  csdb::Address addr_res{};
  switch (type) {
    case ADDR_TYPE::PUBLIC_KEY:
      if (addr.is_public_key() || !findAddrByWalletId(addr.wallet_id(), addr_res))
        addr_res = addr;
      break;
    case ADDR_TYPE::ID:
      uint32_t _id;
      if (findWalletId(addr, _id))
        addr_res = csdb::Address::from_wallet_id(_id);
      break;
  }
  return addr_res;
}

bool BlockChain::is_equal(const csdb::Address &laddr, const csdb::Address &raddr) const {
  if (get_addr_by_type(laddr, ADDR_TYPE::PUBLIC_KEY) == get_addr_by_type(raddr, ADDR_TYPE::PUBLIC_KEY))
    return true;
  return false;
}

#ifdef MONITOR_NODE
uint32_t BlockChain::getTransactionsCount(const csdb::Address& addr) {
  std::lock_guard<decltype(cacheMutex_)> lock(cacheMutex_);
  WalletId id;
  if (addr.is_wallet_id())
    id = addr.wallet_id();
  else if (!walletIds_->normal().find(addr, id))
    return 0;
  const WalletData* wallDataPtr = walletsCacheUpdater_->findWallet(id);
  if (!wallDataPtr)
    return 0;
  return static_cast<uint32_t>(wallDataPtr->transNum_);
}
#endif

#ifdef TRANSACTIONS_INDEX
csdb::TransactionID BlockChain::getLastTransaction(const csdb::Address& addr) {
  std::lock_guard<decltype(cacheMutex_)> lock(cacheMutex_);
  WalletId id;
  if (addr.is_wallet_id())
    id = addr.wallet_id();
  else if (!walletIds_->normal().find(addr, id))
    return csdb::TransactionID();
  const WalletData* wallDataPtr = walletsCacheUpdater_->findWallet(id);
  if (!wallDataPtr)
    return csdb::TransactionID();

  return wallDataPtr->lastTransaction_;
}

csdb::PoolHash BlockChain::getPreviousPoolHash(const csdb::Address& addr, const csdb::PoolHash& ph) {
  std::lock_guard<decltype(dbLock_)> lock(dbLock_);
  return storage_.get_previous_transaction_block(get_addr_by_type(addr, ADDR_TYPE::PUBLIC_KEY), ph);
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
    if(bc_.is_equal(it_->source(), addr_) || bc_.is_equal(it_->target(), addr_))
      break;
  }

  // Oops, no more in this blockfTransactionsListGet
  if (it_ == lapoo_.transactions().rend()) {
    auto ph = bc_.getPreviousPoolHash(addr_, lapoo_.hash());
    lapoo_ = bc_.loadBlock(ph);

    if (lapoo_.is_valid()) {
      it_ = lapoo_.transactions().rbegin();
      // transactions() cannot be empty
      if(!bc_.is_equal(it_->source(), addr_) && !bc_.is_equal(it_->target(), addr_))
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

    for (it_ = lapoo_.transactions().rbegin(); it_ != lapoo_.transactions().rend(); ++it_) {
      if(bc_.is_equal(it_->source(), addr_) || bc_.is_equal(it_->target(), addr_)) {
        found = true;
        break;
      }
    }

    hash = lapoo_.previous_hash();
  }
}

TransactionsIterator::TransactionsIterator(BlockChain& bc, const csdb::Address& addr) : bc_(bc), addr_(addr) {
  setFromHash(bc_.getLastHash());
}

bool TransactionsIterator::isValid() const {
  return lapoo_.is_valid();
}

void TransactionsIterator::next() {
  bool found = false;

  while (++it_ != lapoo_.transactions().rend()) {
    if (bc_.is_equal(it_->source(), addr_) || bc_.is_equal(it_->target(), addr_)) {
      found = true;
      break;
    }
  }

  if (!found) setFromHash(lapoo_.previous_hash());
}

#endif
