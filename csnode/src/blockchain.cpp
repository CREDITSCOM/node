#include <limits>
#include <csdb/currency.h> 
#include <base58.h>
#include <lib/system/logger.hpp>
#include <lib/system/hash.hpp>
#include <lib/system/keys.hpp>
#include "sys/timeb.h"
#include <csnode/blockchain.hpp>

using namespace Credits;

//BlockChain::BlockChain(const char* path)
//{
//	std::cerr << "Trying to open DB..." << std::endl;
//    if (!storage_.open(path))
//    {
//        LOG_ERROR("Couldn't open database at " << path);
//        return;
//    }
//
//    if (!loadCache())
//        return;
//
//    good_ = true;
//}

BlockChain::BlockChain(const std::string& path, csdb::Address genesisAddress, csdb::Address startAddress)
  : good_(false)
  , last_written_sequence(std::numeric_limits<decltype(last_written_sequence)>::max())
  , global_sequence(std::numeric_limits<decltype(last_written_sequence)>::max())
  , blockRequestIsNeeded(false)
  , genesisAddress_(genesisAddress)
  , startAddress_(startAddress)
  , walletIds_(new WalletsIds)
  , walletsCacheStorage_(new WalletsCache(WalletsCache::Config(), genesisAddress, startAddress, *walletIds_))
  , walletsPools_(new WalletsPools(genesisAddress, startAddress, *walletIds_))
{
  bool wasJustCreated = false;
  if (!initFromDB(path, wasJustCreated))
      return;
 
  if (!wasJustCreated)
  {
      std::unique_ptr<WalletsCache::Initer> initer = walletsCacheStorage_->createIniter();
      if (!initCaches(*initer))
          return;
  }

  walletsCacheUpdater_ = walletsCacheStorage_->createUpdater();
 
  good_ = true;
}

bool BlockChain::initFromDB(const std::string& path, bool& wasJustCreated)
{
  std::cout << "Trying to open DB..." << std::endl;
  char  kk[14];
  std::vector <uint8_t> v_hash(32);
  std::vector <csdb::PoolHash> tempHashes;
  csdb::PoolHash temp_hash;
  blockHashes_.reserve(1000000);

  dbs_fname = "test_db/dbs.txt";
  //	hash_offset = 0;
  blockRequestIsNeeded = false;

  bool res = false;
  wasJustCreated = false;
  if (storage_.open(path))
  {
    std::cout << "DB is opened" << std::endl;
    if (storage_.last_hash().is_empty())
    {
      std::cout << "Last hash is empty..." << std::endl;
      if (!storage_.size())
      {
        //std::cout << "Storage is empty... writing genesis block" << std::endl;
        writeGenesisBlock();
        std::ofstream f(dbs_fname);
        if (f.is_open())
        {
          f << "0->0";
          f.close();
          res = true;
          wasJustCreated = true;
        }
      }
      else
      {
        res = false;
        std::cout << "failed!!! Delete the Database!!! It will be restored from nothing..." << std::endl;
      }
    }
    else
    {

      std::cout << "Last hash is not empty..." << std::endl;
      std::ifstream f(dbs_fname);


      if (f.is_open())
      {
        std::cout << "File is opened ... reading" << std::endl;
        f.read(kk, 14);
        f.close();
      }

      char* s_beg = kk;
      char* s_end = strchr(kk, '-');
      *s_end = '\0';
      ht.head = atoi(s_beg);
      s_beg = s_end + 2;
      ht.tag = atoi(s_beg);
      std::cout << "DB structure: " << ht.head << "->" << ht.tag << std::endl;
      setLastWrittenSequence(ht.tag);

      if (loadBlock(storage_.last_hash()).sequence() == ht.tag)
      {
        tempHashes.reserve(ht.tag + 1);
        temp_hash = storage_.last_hash();

        for (uint32_t i = 0; i <= ht.tag; i++)
        {
          tempHashes.push_back(temp_hash);
          //std::cout << "READ> " << temp_hash.to_string() << std::endl;
          temp_hash = loadBlock(temp_hash).previous_hash();
          if (temp_hash.is_empty()) break;
        }
        std::cout << "Hashes read from DB" << std::endl;
        for (auto iter = tempHashes.rbegin(); iter != tempHashes.rend(); ++iter)
        {

          blockHashes_.push_back(*iter);
        }
        std::cout << "Hashes vector converted" << std::endl;
        for (uint32_t i = 0; i <= ht.tag; i++)
        {
          std::cout << "READ> " << i << " : " << blockHashes_.at(i).to_string() << std::endl;
        }
        tempHashes.clear();
        lastHash_ = storage_.last_hash();
        res = true;
      }
      else
      {
        ht.tag = loadBlock(storage_.last_hash()).sequence();
        tempHashes.reserve(ht.tag + 1);
        std::ofstream f(dbs_fname, std::ios::out);
        ht.head = 0;
        bool file_is = f.is_open();
        if (file_is)
        {

          f << ht.head << "->" << ht.tag << std::endl;
          std::cout << "DB structure: " << ht.head << "->" << ht.tag << std::endl;
          std::cout << "DB structure is written succesfully" << std::endl;
          f.close();
        }
        else std::cout << "Error writing DB structure" << std::endl;

        temp_hash = storage_.last_hash();
        for (uint32_t i = 0; i <= ht.tag; i++)
        {
          tempHashes.push_back(temp_hash);
          temp_hash = loadBlock(temp_hash).previous_hash();
          if (temp_hash.is_empty()) break;
        }
        for (auto iter = tempHashes.rbegin(); iter != tempHashes.rend(); ++iter)
        {
          blockHashes_.push_back(*iter);
        }
        for (uint32_t i = 0; i <= ht.tag; i++)
        {
          std::cout << "READ> " << blockHashes_.at(i).to_string() << std::endl;
        }
        tempHashes.clear();

        lastHash_ = storage_.last_hash();
        res = true;
      }
    }
  }
  else
  {
    LOG_ERROR("Couldn't open database at " << path);
    res = false;
  }
  return res;
}

void BlockChain::writeNewBlock(csdb::Pool& pool) {
#ifdef MYLOG
    std::cout << "writeNewBlock is running" << std::endl;
#endif
    putBlock(pool);
}

void BlockChain::writeBlock(csdb::Pool& pool)
{
    TRACE("");

    {
        std::lock_guard<decltype(dbLock_)> l(dbLock_);
        pool.set_storage(storage_);
    }

    if (!pool.compose())
        if (!pool.compose()) {
            LOG_ERROR("Couldn't compose block");
            if (!pool.save())
                return;

        }

    if (!pool.save()) {
        LOG_ERROR("Couldn't save block");
        return;

    }
    std::cout << "Block " << pool.sequence() << " saved succesfully" << std::endl;

    if (!updateWalletsCacheAndPools(pool))
        LOG_ERROR("Couldn't update wallets cache and pools");

    {
        TRACE("");
        std::lock_guard<decltype(waiters_locker)> l(waiters_locker);
        TRACE("");
        new_block_cv.notify_all();
        TRACE("");
    }
}

void BlockChain::setLastWrittenSequence(uint32_t seq) {
    last_written_sequence = seq;
}

uint32_t BlockChain::getLastWrittenSequence() const
{
    return last_written_sequence;
}

void BlockChain::writeGenesisBlock()
{
  LOG_EVENT("Adding the genesis block");

  csdb::Pool genesis;
  csdb::Transaction transaction;
  std::vector<unsigned char> vchRet;

  transaction.set_target(startAddress_);
  transaction.set_source(genesisAddress_);

  transaction.set_currency(csdb::Currency("CS"));
  transaction.set_amount(csdb::Amount(100000000, 0));
  transaction.set_balance(csdb::Amount(100, 0));
  transaction.set_innerID(0);

  genesis.add_transaction(transaction);

  genesis.set_previous_hash(csdb::PoolHash());
  finishNewBlock(genesis);
  std::cout << "Genesis block completed ... trying to save" << std::endl;

  writeNewBlock(genesis);

  setLastWrittenSequence(0);
  global_sequence = 0;
  std::cout << genesis.hash().to_string() << std::endl;
  lastHash_ = genesis.hash();
  blockHashes_.push_back(lastHash_);
  std::cout << "Hash inserted into the hash-vector" << std::endl;
  
  size_t bSize;
  //const char* bl = 
  genesis.to_byte_stream(bSize);
  //std::cout << "GB: " << byteStreamToHex(bl, bSize) << std::endl;
}

csdb::PoolHash BlockChain::getLastHash() const
{
    std::lock_guard<decltype(dbLock_)> l(dbLock_);
    return storage_.last_hash();
}

size_t BlockChain::getSize() const
{
    std::lock_guard<decltype(dbLock_)> l(dbLock_);
    return storage_.size();
}

csdb::Pool BlockChain::loadBlock(const csdb::PoolHash& ph) const
{
    std::lock_guard<decltype(dbLock_)> l(dbLock_);
    auto pool = storage_.pool_load(ph);
    return pool;
}

csdb::Pool BlockChain::loadBlockMeta(const csdb::PoolHash& ph, size_t& cnt) const
{
    std::lock_guard<decltype(dbLock_)> l(dbLock_);
    return storage_.pool_load_meta(ph, cnt);
}

csdb::Transaction BlockChain::loadTransaction(const csdb::TransactionID& transId) const
{
    std::lock_guard<decltype(dbLock_)> l(dbLock_);
    return storage_.transaction(transId);
}

csdb::PoolHash BlockChain::wait_for_block(const csdb::PoolHash &obsolete_block)
{
    TRACE("");
    std::unique_lock<decltype(dbLock_)> l(dbLock_);
    TRACE("");
    csdb::PoolHash res;
    TRACE("");
    new_block_cv.wait(l, [this, &obsolete_block, &res]() {
        TRACE("");
        res = storage_.last_hash();
        TRACE("");
        return obsolete_block != res;
    });
    TRACE("");
    return res;
}

csdb::Address BlockChain::getAddressFromKey(const std::string& key)
{
    std::string pk(static_cast<size_t>(PUBLIC_KEY_LENGTH), '\0');
    std::copy(key.rbegin(),
        std::min(key.rbegin() + PUBLIC_KEY_LENGTH, key.rend()),
        pk.rbegin());
    csdb::Address res = csdb::Address::from_public_key(pk.data());
    return res;
}

csdb::Amount BlockChain::getBalance(const csdb::Address& address) const
{
    if (address.is_wallet_id())
        return getBalance(address.wallet_id());

    std::lock_guard<decltype(cacheMutex_)> lock(cacheMutex_);

    WalletId id{};
    if (!walletIds_->normal().find(address, id))
        return csdb::Amount{};

    return getBalance_Unsafe(id);
}

csdb::Amount BlockChain::getBalance(WalletId id) const
{
    std::lock_guard<decltype(cacheMutex_)> lock(cacheMutex_);

    return getBalance_Unsafe(id);
}

csdb::Amount BlockChain::getBalance_Unsafe(WalletId id) const
{
    const WalletsCache::WalletData* walData = walletsCacheUpdater_->findWallet(id);
    if (!walData)
        return csdb::Amount{};
    return walData->balance_;
}

void BlockChain::finishNewBlock(csdb::Pool& pool)
{
    pool.set_sequence(getLastWrittenSequence() + 1);
    addNewWalletsToPool(pool);
}

void BlockChain::onBlockReceived(csdb::Pool& pool) {
#ifdef MYLOG
    std::cout << "onBlockReceived is running" << std::endl;
#endif

    if (!updateWalletIds(pool, *walletsCacheUpdater_))
        LOG_ERROR("Couldn't update wallet ids");
    putBlock(pool);
}

void BlockChain::putBlock(csdb::Pool& pool)
{
    // Put on top
    std::cout << "---------------------------  Write New Block: " << pool.sequence() << " :  " << pool.transactions_count() << " transactions" << " --------------------------------" << std::endl;
#ifdef MYLOG
    std::cout << "sequence: " << pool.sequence() << ", time: " << pool.user_field(0).value<std::string>().c_str() << std::endl;
    std::cout << " Last      hash: " << lastHash_.to_string() << std::endl;
    std::cout << "Checking Sequence ... ";
#endif
    if (pool.sequence() == getLastWrittenSequence() + 1) {
        std::cout << "OK" << std::endl;
        pool.set_previous_hash(lastHash_);

        std::ofstream f(dbs_fname, std::ios::out);
        ht.head = 0;
        ht.tag = getLastWrittenSequence() + 1;
        bool file_is = f.is_open();
        if (file_is)
        {

            f << ht.head << "->" << ht.tag << std::endl;
#ifdef MYLOG
            std::cout << "DB structure: " << ht.head << "->" << ht.tag << std::endl;
            //std::cout << "DB structure is written succesfully" << std::endl;
#endif
            f.close();
        }
        else std::cout << "Error writing DB structure" << std::endl;

        writeBlock(pool);
        //std::cout << "Preparing to calculate last hash" << std::endl;
        lastHash_ = pool.hash();

        blockHashes_.push_back(lastHash_);
        //std::cout << "Hash inserted into the hash-vector" << std::endl;

        setLastWrittenSequence(pool.sequence());
#ifdef MYLOG
        std::cout << "New last hash: " << lastHash_.to_string() << std::endl;
        std::cout << "New last storage size: " << storage_.size() << std::endl;
#endif
        if (global_sequence == getLastWrittenSequence())
        {
            blockRequestIsNeeded = false;
        }

        return;
    }
    std::cout << "Failed" << std::endl;

    ////////////////////////////////////////////////////////////////////////////////////////////// Syncro!!!
    std::cout << "Chain syncro part ... start " << std::endl;
    global_sequence = pool.sequence();
    blockRequestIsNeeded = true;
}

csdb::PoolHash BlockChain::getLastWrittenHash() const
{
    return lastHash_;
}

//void
//BlockChain::wait_for_block()
//{
//  std::unique_lock<std::mutex> l(dbLock_);
//  auto ls = storage_.size();
//  new_block_cv.wait(l, [ls, this] { return storage_.size() != ls; });
//}

uint32_t BlockChain::getGlobalSequence() const
{
    return global_sequence;
}

csdb::PoolHash BlockChain::getHashBySequence(uint32_t seq) const
{
    return blockHashes_.at(seq);
}

uint32_t BlockChain::getRequestedBlockNumber() const
{
    return (last_written_sequence + 1);
}
void BlockChain::setGlobalSequence(uint32_t seq)
{
    global_sequence = seq;
}

bool BlockChain::getBlockRequestNeed() const
{
    return blockRequestIsNeeded;
}

class BlockChain::TrxLoader
{
public:
    using Transactions = std::vector<csdb::Transaction>;

public:
    TrxLoader(
        csdb::Address wallPubKey,
        BlockChain::WalletId id,
        bool isToLoadWalletsPoolsCache,
        BlockChain& blockchain,
        Transactions& transactions)
        : wallPubKey_(wallPubKey)
        , id_(id)
        , isToLoadWalletsPoolsCache_(isToLoadWalletsPoolsCache)
        , blockchain_(blockchain)
        , transactions_(transactions)
    {
        if (isToLoadWalletsPoolsCache_)
        {
            std::lock_guard<decltype(cacheMutex_)> lock(blockchain_.cacheMutex_);
            blockchain.walletsPools_->addWallet(id);
        }
    }

    bool load(const csdb::PoolHash& poolHash,
        uint64_t& offset,
        uint64_t limit,
        csdb::PoolHash& prevPoolHash)
    {
        csdb::Pool curr = blockchain_.loadBlock(poolHash);
        if (!curr.is_valid())
            return false;

        if (curr.transactions_count())
        {
            bool hasMyTransactions = false;

            for(auto trans : curr.transactions())
            {
                if (transactions_.size() == limit)
                    break;

                // std::cerr << "Ladder: " << trans.target().to_string() << " <-
                // "
                //          << trans.source().to_string() << " of "
                //          << trans.amount().integral() << std::endl;
                if (trans.target() == wallPubKey_ || trans.source() == wallPubKey_)
                {
                    hasMyTransactions = true;

                    if (offset == 0)
                        transactions_.push_back(trans);
                    else
                        --offset;
                }
            }

            if (hasMyTransactions  &&  isToLoadWalletsPoolsCache_)
            {
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

void BlockChain::getTransactions(Transactions& transactions,
    csdb::Address address,
    uint64_t offset,
    uint64_t limit)
{
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

bool BlockChain::findDataForTransactions(
    csdb::Address address,
    csdb::Address& wallPubKey,
    WalletId& id,
    WalletsPools::WalletData::PoolsHashes& hashesArray) const
{
    std::lock_guard<decltype(cacheMutex_)> lock(cacheMutex_);

    if (address.is_wallet_id())
    {
        id = address.wallet_id();

        const WalletData* wallDataPtr = walletsCacheUpdater_->findWallet(id);
        if (!wallDataPtr)
            return false;
        WalletsCache::convert(wallDataPtr->address_, wallPubKey);
    }
    else //if (address.is_public_key())
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

void BlockChain::getTransactions(
    Transactions& transactions,
    csdb::Address wallPubKey,
    WalletId id,
    const WalletsPools::WalletData::PoolsHashes& hashesArray,
    uint64_t offset,
    uint64_t limit)
{
    bool isToLoadWalletsPoolsCache = 
        hashesArray.empty()  && wallPubKey != genesisAddress_  && wallPubKey != startAddress_;
    TrxLoader trxLoader(wallPubKey, id, isToLoadWalletsPoolsCache, *this, transactions);

    csdb::PoolHash prevHash = getLastHash();

    for (size_t i = hashesArray.size() - 1;
        i != std::numeric_limits<decltype(i)>::max();
        --i)
    {
        const auto& poolHashData = hashesArray[i];

        if (poolHashData.trxNum < WalletsPools::WalletData::PoolHashData::maxTrxNum  &&
            poolHashData.trxNum <= offset)
        {
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

    while (true)
    {
        csdb::PoolHash currHash = prevHash;
        if (!trxLoader.load(currHash, offset, limit, prevHash))
            break;
    }
}

bool BlockChain::initCaches(WalletsCache::Initer& initer)
{
    bool res = false;
    try
    {
        csdb::Pool prev = loadBlock(getLastHash());
        while (prev.is_valid())
        {
            if (!updateWalletIds(prev, initer))
                return false;
            initer.loadPrevBlock(prev);

            prev = loadBlock(prev.previous_hash());
        }
        res = true;
    }
    catch (std::exception& e) {
        auto msg = e.what();
        LOG_ERROR("Exc=" << msg);
    }
    catch (...) {
        LOG_ERROR("Exc=...");
    }

    return res;
}

template<typename WalletCacheProcessor>
bool BlockChain::updateWalletIds(const csdb::Pool& pool, WalletCacheProcessor& proc)
{
    try {
        std::lock_guard<decltype(cacheMutex_)> lock(cacheMutex_);

        const csdb::Pool::NewWallets& newWallets = pool.newWallets();
        for (const auto& newWall : newWallets)
        {
            csdb::Address newWallAddress;
            if (!pool.getWalletAddress(newWall, newWallAddress))
            {
                LOG_ERROR("Wrong new wallet data");
                return false;
            }

            if (!insertNewWalletId(newWallAddress, newWall.walletId_, proc))
            {
                LOG_ERROR("Wallet was already added as new");
                return false;
            }
        }
    }
    catch (std::exception& e) {
        auto msg = e.what();
        LOG_ERROR("Exc=" << msg);
        return false;
    }
    catch (...) {
        LOG_ERROR("Exc=...");
        return false;
    }
    return true;
}

bool BlockChain::insertNewWalletId(const csdb::Address& newWallAddress, WalletId newWalletId, WalletsCache::Initer& initer)
{
    WalletId idSpecial{};
    if (!walletIds_->special().insertNormal(newWallAddress, newWalletId, idSpecial))
    {
        LOG_ERROR("Cannot add new wallet");
        return false;
    }
    if (WalletsIds::Special::isSpecial(idSpecial))
    {
        if (!initer.moveData(idSpecial, newWalletId))
        {
            LOG_ERROR("Cannot move special wallet id data to newWalletId: idSpecial=" << idSpecial << " newWalletId=" << newWalletId);
            return false;
        }
    }
    return true;
}

bool BlockChain::insertNewWalletId(const csdb::Address& newWallAddress, WalletId newWalletId, WalletsCache::Updater&)
{
    if (!walletIds_->normal().insert(newWallAddress, newWalletId))
    {
        LOG_ERROR("Cannot add new wallet");
        return false;
    }
    return true;
}

void BlockChain::addNewWalletToPool(const csdb::Address& walletAddress, const csdb::Pool::NewWalletInfo::AddressId& addressId, csdb::Pool::NewWallets& newWallets)
{
    if (!walletAddress.is_public_key())
        return;
    if (walletAddress == genesisAddress_)
        return;

    WalletId id{};
    if (getWalletId(walletAddress, id))
    {
        newWallets.emplace_back(csdb::Pool::NewWalletInfo{ addressId, id });
    }
}

void BlockChain::addNewWalletsToPool(csdb::Pool& pool)
{
    csdb::Pool::NewWallets* newWallets = pool.newWallets();
    if (!newWallets)
    {
        LOG_ERROR("Pool is read-only");
        return;
    }
    newWallets->clear();

    csdb::Pool::Transactions& transactions = pool.transactions();
    for (size_t idx = 0; idx < transactions.size(); ++idx)
    {
        {
            csdb::Pool::NewWalletInfo::AddressId addressId = { idx, csdb::Pool::NewWalletInfo::AddressType::AddressIsSource };
            addNewWalletToPool(transactions[idx].source(), addressId, *newWallets);
        }
        {
            csdb::Pool::NewWalletInfo::AddressId addressId = { idx, csdb::Pool::NewWalletInfo::AddressType::AddressIsTarget };
            addNewWalletToPool(transactions[idx].target(), addressId, *newWallets);
        }
    }
}

bool BlockChain::updateWalletsCacheAndPools(csdb::Pool& nextPool)
{
    if (!walletsCacheUpdater_)
    {
        LOG_ERROR("!walletsCacheUpdater");
        return false;
    }

    try {
        std::lock_guard<decltype(cacheMutex_)> lock(cacheMutex_);

        walletsCacheUpdater_->loadNextBlock(nextPool);
        walletsPools_->loadNextBlock(nextPool);
    }
    catch (std::exception& e) {
        auto msg = e.what();
        LOG_ERROR("Exc=" << msg);
        return false;
    }
    catch (...) {
        LOG_ERROR("Exc=...");
        return false;
    }
    return true;
}

bool BlockChain::findWalletData(const csdb::Address& address, WalletData& wallData) const
{
    if (address.is_wallet_id())
        return findWalletData(address.wallet_id(), wallData);

    std::lock_guard<decltype(cacheMutex_)> lock(cacheMutex_);

    WalletId id{};
    if (!walletIds_->normal().find(address, id))
        return false;

    return findWalletData_Unsafe(id, wallData);
}

bool BlockChain::findWalletData(WalletId id, WalletData& wallData) const
{
    std::lock_guard<decltype(cacheMutex_)> lock(cacheMutex_);
    return findWalletData_Unsafe(id, wallData);
}

bool BlockChain::findWalletData_Unsafe(WalletId id, WalletData& wallData) const
{
    const WalletData* wallDataPtr = walletsCacheUpdater_->findWallet(id);
    if (wallDataPtr)
    {
        wallData = *wallDataPtr;
        return true;
    }
    return false;
}

bool BlockChain::getModifiedWallets(Mask& dest) const
{
    std::lock_guard<decltype(cacheMutex_)> lock(cacheMutex_);

    bool isNewModified =
        (walletsCacheUpdater_->getModified().size() != dest.size()) ||
        walletsCacheUpdater_->getModified().any();

    dest.resize(walletsCacheUpdater_->getModified().size(), true);
    dest |= walletsCacheUpdater_->getModified();
    return isNewModified;
}

bool BlockChain::insertWalletId(const WalletAddress& address, WalletId id)
{
    if (address.is_wallet_id())
    {
        id = address.wallet_id();
        return true;
    }
    else if (address.is_public_key())
    {
        std::lock_guard<decltype(cacheMutex_)> lock(cacheMutex_);
        return walletIds_->normal().insert(address, id);
    }
    LOG_ERROR("Wrong address");
    return false;
}

bool BlockChain::findWalletId(const WalletAddress& address, WalletId& id) const
{
    if (address.is_wallet_id())
    {
        id = address.wallet_id();
        return true;
    }
    else if (address.is_public_key())
    {
        std::lock_guard<decltype(cacheMutex_)> lock(cacheMutex_);
        return walletIds_->normal().find(address, id);
    }
    LOG_ERROR("Wrong address");
    return false;
}

bool BlockChain::getWalletId(const WalletAddress& address, WalletId& id)
{
    if (address.is_wallet_id())
    {
        id = address.wallet_id();
        return true;
    }
    else if (address.is_public_key())
    {
        std::lock_guard<decltype(cacheMutex_)> lock(cacheMutex_);
        return walletIds_->normal().get(address, id);
    }
    LOG_ERROR("Wrong address");
    return false;
}
