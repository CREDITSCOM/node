#include <limits>
#include <csdb/currency.h> 
#include <base58.h>
#include <lib/system/logger.hpp>
#include <lib/system/hash.hpp>
#include <lib/system/keys.hpp>
#include "sys/timeb.h"
#include <csnode/BlockHashes.h>
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
  , global_sequence(static_cast<decltype(global_sequence)>(-1))
  , blockRequestIsNeeded(false)
  , genesisAddress_(genesisAddress)
  , startAddress_(startAddress)
  , walletIds_(new WalletsIds)
  , walletsCacheStorage_(new WalletsCache(WalletsCache::Config(), genesisAddress, startAddress, *walletIds_))
  , walletsPools_(new WalletsPools(genesisAddress, startAddress, *walletIds_))
{
  std::cout << "Trying to open DB..." << std::endl;
  if (!storage_.open(path))
  {
    LOG_ERROR("Couldn't open database at " << path);
    return;
  }
  std::cout << "DB is opened" << std::endl;

  blockHashes_.reset(new Credits::BlockHashes("test_db/dbs.txt"));

  if (storage_.last_hash().is_empty())
  {
      std::cout << "Last hash is empty..." << std::endl;
      if (storage_.size())
      {
          std::cout << "failed!!! Delete the Database!!! It will be restored from nothing..." << std::endl;
          return;
      }
      walletsCacheUpdater_ = walletsCacheStorage_->createUpdater();
      if (!writeGenesisBlock())
      {
          std::cout << "failed!!! Cannot write genesis block" << std::endl;
          return;
      }
  }
  else
  {
      std::cout << "Last hash is not empty..." << std::endl;
      {
          std::unique_ptr<WalletsCache::Initer> initer = walletsCacheStorage_->createIniter();
          if (!initFromDB(*initer))
              return;
          if (!initer->isFinishedOk())
          {
              std::cout << "Initialization from DB finished with error" << std::endl;
              return;
          }
      }
      walletsCacheUpdater_ = walletsCacheStorage_->createUpdater();
  }

  good_ = true;
}

BlockChain::~BlockChain()
{
}

bool BlockChain::initFromDB(Credits::WalletsCache::Initer& initer)
{
    bool res = false;
    try
    {
        //blockHashes_->loadDbStructure();
        blockHashes_->initStart();

        csdb::Pool prev = loadBlock(getLastHash());

        while (prev.is_valid())
        {
            if (!updateWalletIds(prev, initer))
                return false;
            initer.loadPrevBlock(prev);
            if (!blockHashes_->initFromPrevBlock(prev))
                return false;

            prev = loadBlock(prev.previous_hash());
        }

        blockHashes_->initFinish();
        blockHashes_->saveDbStructure();

        lastHash_ = getLastHash();

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

bool BlockChain::writeNewBlock(csdb::Pool& pool) {
#ifdef MYLOG
    std::cout << "writeNewBlock is running" << std::endl;
#endif
    return putBlock(pool);
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

    if (!updateFromNextBlock(pool))
        LOG_ERROR("Couldn't update from next block");

    {
        TRACE("");
        std::lock_guard<decltype(waiters_locker)> l(waiters_locker);
        TRACE("");
        new_block_cv.notify_all();
        TRACE("");
    }
}

uint32_t BlockChain::getLastWrittenSequence() const
{
    return (blockHashes_->empty()) ? static_cast<uint32_t>(-1) : static_cast<uint32_t>(blockHashes_->getDbStructure().last_);
}

bool BlockChain::writeGenesisBlock()
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

  if (!writeNewBlock(genesis))
      return false;

  global_sequence = genesis.sequence();
  std::cout << genesis.hash().to_string() << std::endl;

  size_t bSize;
  //const char* bl = 
  genesis.to_byte_stream(bSize);
  //std::cout << "GB: " << byteStreamToHex(bl, bSize) << std::endl;
  return true;
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

bool BlockChain::finishNewBlock(csdb::Pool& pool)
{
    pool.set_sequence(getLastWrittenSequence() + 1);
    addNewWalletsToPool(pool);
    return true;
}

bool BlockChain::onBlockReceived(csdb::Pool& pool) {
#ifdef MYLOG
    std::cout << "onBlockReceived is running" << std::endl;
#endif

    if (!updateWalletIds(pool, *walletsCacheUpdater_))
    {
        LOG_ERROR("Couldn't update wallet ids");
        return false;
    }
    return putBlock(pool);
}

bool BlockChain::putBlock(csdb::Pool& pool)
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

        writeBlock(pool);
        //std::cout << "Preparing to calculate last hash" << std::endl;

        lastHash_ = pool.hash();

#ifdef MYLOG
        std::cout << "New last hash: " << lastHash_.to_string() << std::endl;
        std::cout << "New last storage size: " << storage_.size() << std::endl;
#endif
        if (global_sequence == getLastWrittenSequence())
        {
            blockRequestIsNeeded = false;
        }
        return true;
    }
    else
    {
        std::cout << "Failed" << std::endl;

        ////////////////////////////////////////////////////////////////////////////////////////////// Syncro!!!
        std::cout << "Chain syncro part ... start " << std::endl;
        global_sequence = pool.sequence();
        blockRequestIsNeeded = true;
        return false;
    }
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
    csdb::PoolHash res{};
    if (!blockHashes_->find(seq, res))
        return csdb::PoolHash{};
    return res;
}

uint32_t BlockChain::getRequestedBlockNumber() const
{
    return getLastWrittenSequence() + 1;
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

bool BlockChain::updateFromNextBlock(csdb::Pool& nextPool)
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

        blockHashes_->loadNextBlock(nextPool);
        //std::cout << "Hash inserted into the hash-vector" << std::endl;
        if (!blockHashes_->saveDbStructure())
            std::cout << "Error writing DB structure" << std::endl;
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

bool BlockChain::findWalletData(const csdb::Address& address, WalletData& wallData, WalletId& id) const
{
    if (address.is_wallet_id())
    {
        id = address.wallet_id();
        return findWalletData(address.wallet_id(), wallData);
    }

    std::lock_guard<decltype(cacheMutex_)> lock(cacheMutex_);

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
        return false;
    }
    else if (address.is_public_key())
    {
        std::lock_guard<decltype(cacheMutex_)> lock(cacheMutex_);
        return walletIds_->normal().get(address, id);
    }
    LOG_ERROR("Wrong address");
    return false;
}
