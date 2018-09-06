#include <limits>
#include <csdb/currency.h> 
#include <base58.h>
#include <lib/system/logger.hpp>
#include <lib/system/hash.hpp>
#include <lib/system/keys.hpp>

#include "wallets_cache.hpp"
#include "blockchain.hpp"

#include "sys/timeb.h"

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

BlockChain::BlockChain(const char* path) {

  if (!loadCache())
     return;

  std::cout << "Trying to open DB..." << std::endl;
  char  kk[14];
  std::vector <uint8_t> v_hash(32);
  std::vector <csdb::PoolHash> tempHashes;
  csdb::PoolHash temp_hash;
  blockHashes_.reserve(1000000);

  dbs_fname = "test_db/dbs.txt";
  bool file_is;
  //	hash_offset = 0;
  blockRequestIsNeeded = false;


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
        file_is = f.is_open();
        if (file_is)
        {
          f << "0->0";
          good_ = true;
          f.close();
        }
      }
      else
      {
        good_ = false;
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
        good_ = true;
        return;

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
        good_ = true;
        return;
      }
    }
  }
  else
  {
    LOG_ERROR("Couldn't open database at " << path);
    good_ = false;
  }
  return;
}

void BlockChain::putBlock(csdb::Pool& pool) {
#ifdef MYLOG
  std::cout << "Put block is running" << std::endl;
  #endif
  onBlockReceived(pool);
}


void BlockChain::writeBlock(csdb::Pool& pool) {
	TRACE("");
	
	{
		std::lock_guard<decltype(dbLock_)> l(dbLock_);
		pool.set_storage(storage_);
	}

	//	std::cout << "OK" << std::endl << "Pool is composing ... ";
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
	{
		TRACE("");
		std::lock_guard<decltype(waiters_locker)> l(waiters_locker);
		TRACE("");
		new_block_cv.notify_all();
		TRACE("");
	}
	
	if (!updateCache(pool)) {
		LOG_ERROR("Couldn't update cache");
	}
}

void BlockChain::setLastWrittenSequence(uint32_t seq) {
  last_written_sequence = seq;
}

uint32_t BlockChain::getLastWrittenSequence()
{
  return last_written_sequence;
}

void BlockChain::writeGenesisBlock() {
  LOG_EVENT("Adding the genesis block");

  csdb::Pool genesis;
  csdb::Transaction transaction;
  std::vector<unsigned char> vchRet;

  transaction.set_source(csdb::Address::from_string("0000000000000000000000000000000000000000000000000000000000000002"));
  DecodeBase58("5B3YXqDTcWQFGAqEJQJP3Bg1ZK8FFtHtgCiFLT5VAxpe", vchRet); // 4 3rUevsW5xfob6qDxWMDFwwTQCq39SYhzstuyfUGSDvF2QHBRyPD8fSk49wFXaPk3GztfxtuU85QHfMV3ozfqa7rN
  transaction.set_target(csdb::Address::from_string(byteStreamToHex((const char*)vchRet.data(), 32)));
  transaction.set_currency(csdb::Currency("CS"));
  transaction.set_amount(csdb::Amount(std::numeric_limits<int32_t>::max(), 0));
  transaction.set_balance(csdb::Amount(0, 0));
  transaction.set_innerID(0);
  vchRet.clear();
  genesis.add_transaction(transaction);

  genesis.set_previous_hash(csdb::PoolHash());
  genesis.set_sequence(0);
  setLastWrittenSequence(0);
  std::cout << "Genesis block completed ... trying to save" << std::endl;

  writeBlock(genesis);
  global_sequence = 0;
  std::cout << genesis.hash().to_string() << std::endl;
  lastHash_ = genesis.hash();
  blockHashes_.push_back(lastHash_);
  std::cout << "Hash inserted into the hash-vector" << std::endl;
  
  size_t bSize;
  const char* bl = genesis.to_byte_stream(bSize);
  //std::cout << "GB: " << byteStreamToHex(bl, bSize) << std::endl;
}

bool
BlockChain::loadCache()
{
    try {
        std::lock_guard<decltype(cacheMutex_)> lock(cacheMutex_);
        walletsCache_.reset(new WalletsCache(WalletsCache::Config()));
        walletsCache_->load(*this);
    } catch (std::exception& e) {
        auto msg = e.what();
        LOG_ERROR("Exc=" << msg);
        return false;
    } catch (...) {
        LOG_ERROR("Exc=...");
        return false;
    }
    return true;
}

bool
BlockChain::updateCache(csdb::Pool& pool)
{
    try {
        std::lock_guard<decltype(cacheMutex_)> lock(cacheMutex_);
        walletsCache_->updateFrom(pool);
    } catch (std::exception& e) {
        auto msg = e.what();
        LOG_ERROR("Exc=" << msg);
        return false;
    } catch (...) {
        LOG_ERROR("Exc=...");
        return false;
    }
    return true;
}

void
BlockChain::writeLastBlock(csdb::Pool& pool)
{
    TRACE("");
    std::lock_guard<decltype(dbLock_)> l(dbLock_);

    pool.set_storage(storage_);
    pool.set_previous_hash(storage_.last_hash());
    pool.set_sequence(storage_.size());

    if (!pool.compose()) {
        LOG_ERROR("Couldn't compose block");
        return;
    }

    if (!pool.save()) {
        LOG_ERROR("Couldn't save block");
        return;
    }
    {
        TRACE("");
        std::lock_guard<decltype(waiters_locker)> l(waiters_locker);
        TRACE("");
        new_block_cv.notify_all();
        TRACE("");
    }

    if (!updateCache(pool)) {
        LOG_ERROR("Couldn't update cache");
    }
}

csdb::PoolHash
BlockChain::getLastHash() const
{
    std::lock_guard<decltype(dbLock_)> l(dbLock_);
    return storage_.last_hash();
}

size_t
BlockChain::getSize() const
{
    std::lock_guard<decltype(dbLock_)> l(dbLock_);
    return storage_.size();
}

csdb::Pool
BlockChain::loadBlock(const csdb::PoolHash& ph) const
{
    std::lock_guard<decltype(dbLock_)> l(dbLock_);
    auto pool = storage_.pool_load(ph);
    return pool;
}

csdb::Pool
BlockChain::loadBlockMeta(const csdb::PoolHash& ph, size_t& cnt) const
{
    std::lock_guard<decltype(dbLock_)> l(dbLock_);
    return storage_.pool_load_meta(ph, cnt);
}

csdb::Transaction
BlockChain::loadTransaction(const csdb::TransactionID& transId) const
{
    std::lock_guard<decltype(dbLock_)> l(dbLock_);
    return storage_.transaction(transId);
}

csdb::PoolHash
BlockChain::wait_for_block(const csdb::PoolHash &obsolete_block)
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

csdb::Address
BlockChain::getAddressFromKey(const std::string& key)
{
  std::string pk(static_cast<size_t>(PUBLIC_KEY_LENGTH), '\0');
    std::copy(key.rbegin(),
            std::min(key.rbegin() + PUBLIC_KEY_LENGTH, key.rend()),
              pk.rbegin());
    csdb::Address res = csdb::Address::from_public_key(pk.data());
    return res;
}

csdb::Amount
BlockChain::getBalance(const csdb::Address& address) const
{
    {
        std::lock_guard<decltype(cacheMutex_)> lock(cacheMutex_);
        const WalletsCache::WalletData* walData =
          walletsCache_->findWallet(address.public_key());
        if (walData)
            return walData->balance_;
    }
    return calcBalance(address);
}

csdb::Amount
BlockChain::calcBalance(csdb::Address address) const
{
    csdb::Amount result(0);

    csdb::Pool curr = loadBlock(getLastHash());
    while (curr.is_valid()) {
        for (size_t i = 0; i < curr.transactions_count(); i++) {
            csdb::Transaction tr = curr.transaction(i);
            if (tr.source() == address)
                result -= tr.amount();
            else if (tr.target() == address)
                result += tr.amount();
        }
        curr = loadBlock(curr.previous_hash());
    }
    return result;
}


void BlockChain::onBlockReceived(csdb::Pool& pool)
{
  // Put on top
  std::cout << "---------------------------  Write New Block: " << pool.sequence() << " :  " << pool.transactions_count() << " transactions"<<" --------------------------------" << std::endl;
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

csdb::PoolHash BlockChain::getLastWrittenHash()
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


uint32_t BlockChain::getGlobalSequence()
{
  return global_sequence;
}

csdb::PoolHash BlockChain::getHashBySequence(uint32_t seq)
{
  return blockHashes_.at(seq);
}

uint32_t BlockChain::getRequestedBlockNumber()
{
  return (last_written_sequence + 1);
}
void BlockChain::setGlobalSequence(uint32_t seq)
{
  global_sequence = seq;
}

bool BlockChain::getBlockRequestNeed()
{
  return blockRequestIsNeeded;
}


namespace
{
    class TrxLoader
    {
    public:
        using Transaction = std::vector<csdb::Transaction>;
    public:
        TrxLoader(csdb::Address addr, const BlockChain& blockchain, Transaction& transactions)
            : addr_(addr), blockchain_(blockchain), transactions_(transactions)
        {}

    bool load(const csdb::PoolHash& poolHash,
              int64_t& offset,
              int64_t limit,
              csdb::PoolHash& prevPoolHash)
    {
        csdb::Pool curr = blockchain_.loadBlock(poolHash);
        if (!curr.is_valid())
            return false;

        if (curr.transactions_count()) {
            auto curIdx = static_cast<csdb::TransactionID::sequence_t>(
              curr.transactions_count() - 1);

            while (true) {
                auto trans = curr.transaction(curIdx);
                // std::cerr << "Ladder: " << trans.target().to_string() << " <-
                // "
                //          << trans.source().to_string() << " of "
                //          << trans.amount().integral() << std::endl;
                if (trans.target() == addr_ || trans.source() == addr_) {
                    if (offset == 0)
                        transactions_.push_back(trans);
                    else
                        --offset;
                }

                if (transactions_.size() == limit)
                    break;

                if (curIdx == 0)
                    break;
                --curIdx;
            }
        }

        prevPoolHash = curr.previous_hash();
        return true;
    }

  private:
    csdb::Address addr_;
    const BlockChain& blockchain_;
    Transaction& transactions_;
};
}

void
BlockChain::getTransactions(Transactions& transactions,
                            csdb::Address address,
                            int64_t offset,
                            const int64_t limit) const
{
    TrxLoader trxLoader(address, *this, transactions);

    WalletsCache::WalletData::PoolsHashes hashesArray;
    {
        std::lock_guard<decltype(cacheMutex_)> lock(cacheMutex_);
        const WalletsCache::WalletData* walData =
          walletsCache_->findWallet(address.public_key());
        if (walData) {
            hashesArray = walData->poolsHashes_;
        }
    }

    csdb::PoolHash prevHash = getLastHash();
    if (!hashesArray.empty()) {
        for (size_t i = hashesArray.size() - 1;
             i != std::numeric_limits<decltype(i)>::max();
             --i) {
            const auto& poolHashData = hashesArray[i];

            if (poolHashData.trxNum <
                  WalletsCache::WalletData::PoolHashData::maxTrxNum &&
                poolHashData.trxNum <= offset) {
                offset -= poolHashData.trxNum;
                continue;
            }

            csdb::PoolHash currHash;
            WalletsCache::convert(poolHashData.poolHash, currHash);

            if (!trxLoader.load(currHash, offset, limit, prevHash))
                return;
            if (transactions.size() >= size_t(limit))
                return;
        }
    }

    while (true) {
        csdb::PoolHash currHash = prevHash;
        if (!trxLoader.load(currHash, offset, limit, prevHash))
            break;
    }
}