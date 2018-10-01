#include <limits>
#include <csdb/currency.h>
#include <base58.h>
#include <lib/system/logger.hpp>
#include <lib/system/hash.hpp>
#include <lib/system/keys.hpp>
#include <lib/system/utils.hpp>

#include "wallets_cache.hpp"
#include "blockchain.hpp"

#include "sys/timeb.h"

using namespace Credits;

BlockChain::BlockChain(const char* path) {
  if (!loadCache()) {
    return;
  }

  cslog() << "Trying to open DB...";

  char  kk[14];
  std::vector <uint8_t> v_hash(32);
  std::vector <csdb::PoolHash> tempHashes;
  csdb::PoolHash temp_hash;
  blockHashes_.reserve(1000000);

  dbs_fname = "test_db/dbs.txt";

  bool file_is;
  blockRequestIsNeeded = false;

  if (storage_.open(path))
  {
    cslog() << "DB is opened";

    if (storage_.last_hash().is_empty())
    {
      csdebug() << "Last hash is empty...";

      if (!storage_.size())
      {
        csdebug() << "Storage is empty... writing genesis block";

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

        csdebug() << "failed!!! Delete the Database!!! It will be restored from nothing...";
      }
    }
    else
    {
      csdebug() << "Last hash is not empty...";

      std::ifstream f(dbs_fname);
      if (f.is_open())
      {
        csdebug() << "File is opened ... reading";

        f.read(kk, 14);
        f.close();
      }

      char* s_beg = kk;
      char* s_end = strchr(kk, '-');
      *s_end = '\0';
      ht.head = atoi(s_beg);
      s_beg = s_end + 2;
      ht.tag = atoi(s_beg);

      csdebug() << "DB structure: " << ht.head << "->" << ht.tag;

      setLastWrittenSequence(ht.tag);
      if (loadBlock(storage_.last_hash()).sequence() == ht.tag)
      {
        tempHashes.reserve(ht.tag + 1);
        temp_hash = storage_.last_hash();

        for (uint32_t i = 0; i <= ht.tag; ++i)
        {
          tempHashes.push_back(temp_hash);

          csdebug() << "READ> " << temp_hash.to_string();

          temp_hash = loadBlock(temp_hash).previous_hash();
          if (temp_hash.is_empty()) break;
        }

        csdebug() << "Hashes read from DB";

        for (auto iter = tempHashes.rbegin(); iter != tempHashes.rend(); ++iter)
        {
          blockHashes_.push_back(*iter);
        }

        csdebug() << "Hashes vector converted";

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

          csdebug() << "DB structure: " << ht.head << "->" << ht.tag;
          csdebug() << "DB structure is written succesfully";

          f.close();
        }
        else {
          cslog() << "Error writing DB structure";
        }

        temp_hash = storage_.last_hash();

        for (uint32_t i = 0; i <= ht.tag; ++i)
        {
          tempHashes.push_back(temp_hash);
          temp_hash = loadBlock(temp_hash).previous_hash();

          if (temp_hash.is_empty()) {
            break;
          }
        }

        for (auto iter = tempHashes.rbegin(); iter != tempHashes.rend(); ++iter) {
          blockHashes_.push_back(*iter);
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
    cserror() << "Couldn't open database at " << path;
    good_ = false;
  }

  return;
}

void BlockChain::putBlock(csdb::Pool& pool) {
  csdebug() << "Put block is running";

  onBlockReceived(pool);
}

void BlockChain::writeBlock(csdb::Pool& pool) {
  {
    std::lock_guard<decltype(dbLock_)> l(dbLock_);
    pool.set_storage(storage_);
  }

  if (!pool.compose()) {
    cserror() << "Couldn't compose block";
    return;
  }

  if (!pool.save()) {
    cserror() <<"Couldn't save block";
    return;
  }

  cslog() << "Block " << pool.sequence() << " saved succesfully";

  {
    std::lock_guard<decltype(waiters_locker)> l(waiters_locker);
    new_block_cv.notify_all();
  }

  if (!updateCache(pool)) {
    cserror() << "Couldn't update cache";
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
  csdebug() << "Adding the genesis block";

  csdb::Pool genesis;
  csdb::Transaction transaction;
  std::vector<unsigned char> vchRet;

  transaction.set_target(csdb::Address::from_string("0000000000000000000000000000000000000000000000000000000000000002"));
  transaction.set_source(csdb::Address::from_string("0000000000000000000000000000000000000000000000000000000000000001"));

  transaction.set_currency(csdb::Currency("CS"));
  transaction.set_amount(csdb::Amount(1000000000, 0));
  transaction.set_balance(csdb::Amount(0, 0));
  transaction.set_innerID(0);

  genesis.add_transaction(transaction);

  //key1
  transaction.set_source(csdb::Address::from_string("0000000000000000000000000000000000000000000000000000000000000002"));
  DecodeBase58("nDJKdm29yibmNwGfwKpdEh1xMnPQic2NRMBNd33RfVg", vchRet);
  transaction.set_target(csdb::Address::from_string(cs::Utils::byteStreamToHex((const char*)vchRet.data(), 32)));
  transaction.set_currency(csdb::Currency("CS"));
  transaction.set_amount(csdb::Amount(100000000, 0));
  transaction.set_balance(csdb::Amount(1000000000, 0));
  transaction.set_innerID(0);
  vchRet.clear();
  genesis.add_transaction(transaction);

  //key2
  transaction.set_source(csdb::Address::from_string("0000000000000000000000000000000000000000000000000000000000000002"));
  DecodeBase58("mmM3sXYkK5m12R7NiwcS3ExAocJxuN8BVfmgtyhioUS", vchRet);
  transaction.set_target(csdb::Address::from_string(cs::Utils::byteStreamToHex((const char*)vchRet.data(), 32)));
  transaction.set_currency(csdb::Currency("CS"));
  transaction.set_amount(csdb::Amount(1000000000, 0));
  transaction.set_balance(csdb::Amount(1000000000, 0));
  transaction.set_innerID(1);
  vchRet.clear();
  genesis.add_transaction(transaction);

  //key3
  transaction.set_source(csdb::Address::from_string("0000000000000000000000000000000000000000000000000000000000000002"));
  DecodeBase58("BRQaq2v7PP95YXCu4qfSFkwszTzvQFQsYZSA72bXExaN", vchRet);
  transaction.set_target(csdb::Address::from_string(cs::Utils::byteStreamToHex((const char*)vchRet.data(), 32)));

  transaction.set_currency(csdb::Currency("CS"));
  transaction.set_amount(csdb::Amount(100000000, 0));
  transaction.set_balance(csdb::Amount(1000000000, 0));
  transaction.set_innerID(2);
  vchRet.clear();
  genesis.add_transaction(transaction);

  //key4
  transaction.set_source(csdb::Address::from_string("0000000000000000000000000000000000000000000000000000000000000002"));
  DecodeBase58("8jP98VUhiB3NQ9SQA4DKjkTrqDhVq26D6AjvwLekWVU9", vchRet);
  transaction.set_target(csdb::Address::from_string(cs::Utils::byteStreamToHex((const char*)vchRet.data(), 32)));

  transaction.set_currency(csdb::Currency("CS"));
  transaction.set_amount(csdb::Amount(100000000, 0));
  transaction.set_balance(csdb::Amount(1000000000, 0));
  transaction.set_innerID(3);
  vchRet.clear();
  genesis.add_transaction(transaction);

  //key5
  transaction.set_source(csdb::Address::from_string("0000000000000000000000000000000000000000000000000000000000000002"));
  DecodeBase58("ESi7fZAuRmRmYSr57zT7o2sDkyxQo5wg5vSUvqb6MsTG", vchRet);
  transaction.set_target(csdb::Address::from_string(cs::Utils::byteStreamToHex((const char*)vchRet.data(), 32)));

  transaction.set_currency(csdb::Currency("CS"));
  transaction.set_amount(csdb::Amount(100000000, 0));
  transaction.set_balance(csdb::Amount(1000000000, 0));
  transaction.set_innerID(4);
  vchRet.clear();
  genesis.add_transaction(transaction);

  //key6
  transaction.set_source(csdb::Address::from_string("0000000000000000000000000000000000000000000000000000000000000002"));
  DecodeBase58("4tEQbQPYZq1bZ8Tn9DpCXYUgPgEgcqsBPXX4fXef7FuL", vchRet);
  transaction.set_target(csdb::Address::from_string(cs::Utils::byteStreamToHex((const char*)vchRet.data(), 32)));

  transaction.set_currency(csdb::Currency("CS"));
  transaction.set_amount(csdb::Amount(100000000, 0));
  transaction.set_balance(csdb::Amount(1000000000, 0));
  transaction.set_innerID(5);
  vchRet.clear();
  genesis.add_transaction(transaction);

  //key7
  transaction.set_source(csdb::Address::from_string("0000000000000000000000000000000000000000000000000000000000000002"));
  DecodeBase58("H5ptdUUfjJBGiK2X3gN2EzNYxituCUUnXv2tiMdQKP3b", vchRet);
  transaction.set_target(csdb::Address::from_string(cs::Utils::byteStreamToHex((const char*)vchRet.data(), 32)));

  transaction.set_currency(csdb::Currency("CS"));
  transaction.set_amount(csdb::Amount(100000000, 0));
  transaction.set_balance(csdb::Amount(1000000000, 0));
  transaction.set_innerID(6);
  vchRet.clear();
  genesis.add_transaction(transaction);

  //key8
  transaction.set_source(csdb::Address::from_string("0000000000000000000000000000000000000000000000000000000000000002"));
  DecodeBase58("FuvGENwrCY2M6hCeWhCUMEx5uWg71q1oY3TuxDtGcMnN", vchRet);
  transaction.set_target(csdb::Address::from_string(cs::Utils::byteStreamToHex((const char*)vchRet.data(), 32)));

  transaction.set_currency(csdb::Currency("CS"));
  transaction.set_amount(csdb::Amount(100000000, 0));
  transaction.set_balance(csdb::Amount(1000000000, 0));
  transaction.set_innerID(7);
  vchRet.clear();
  genesis.add_transaction(transaction);

  //key9
  transaction.set_source(csdb::Address::from_string("0000000000000000000000000000000000000000000000000000000000000002"));
  DecodeBase58("4qVXL76hqkvxh3jZKGZjbxMvBirWRibb2zRdS5TrwJga", vchRet);
  transaction.set_target(csdb::Address::from_string(cs::Utils::byteStreamToHex((const char*)vchRet.data(), 32)));

  transaction.set_currency(csdb::Currency("CS"));
  transaction.set_amount(csdb::Amount(100000000, 0));
  transaction.set_balance(csdb::Amount(1000000000, 0));
  transaction.set_innerID(8);
  vchRet.clear();
  genesis.add_transaction(transaction);

  //key10
  transaction.set_source(csdb::Address::from_string("0000000000000000000000000000000000000000000000000000000000000002"));
  DecodeBase58("7DmZqQhMjPR7ptJUQh4sRsyERgbK8DeFE3F5vbJFpu1D", vchRet);
  transaction.set_target(csdb::Address::from_string(cs::Utils::byteStreamToHex((const char*)vchRet.data(), 32)));

  transaction.set_currency(csdb::Currency("CS"));
  transaction.set_amount(csdb::Amount(100000000, 0));
  transaction.set_balance(csdb::Amount(1000000000, 0));
  transaction.set_innerID(9);
  vchRet.clear();
  genesis.add_transaction(transaction);


  genesis.set_previous_hash(csdb::PoolHash());
  genesis.set_sequence(0);
  setLastWrittenSequence(0);

  csdebug() << "Genesis block completed ... trying to save";

  writeBlock(genesis);
  global_sequence = 0;

  cslog() << genesis.hash().to_string();

  lastHash_ = genesis.hash();
  blockHashes_.push_back(lastHash_);

  csdebug() << "Hash inserted into the hash-vector";
}

bool BlockChain::loadCache()
{
    try {
        std::lock_guard<decltype(cacheMutex_)> lock(cacheMutex_);
        walletsCache_.reset(new WalletsCache(WalletsCache::Config()));
        walletsCache_->load(*this);
    } catch (std::exception& e) {
        cserror() << "Exc=" << e.what();
        return false;
    } catch (...) {
        cserror() << "Exc=...";
        return false;
    }

    return true;
}

bool BlockChain::updateCache(csdb::Pool& pool)
{
    try {
        std::lock_guard<decltype(cacheMutex_)> lock(cacheMutex_);
        walletsCache_->updateFrom(pool);
    } catch (std::exception& e) {
        LOG_ERROR("Exc=" << e.what());
        return false;
    } catch (...) {
        LOG_ERROR("Exc=...");
        return false;
    }
    return true;
}

void BlockChain::writeLastBlock(csdb::Pool& pool)
{
    std::lock_guard<decltype(dbLock_)> l(dbLock_);

    pool.set_storage(storage_);
    pool.set_previous_hash(storage_.last_hash());
    pool.set_sequence(storage_.size());

    if (!pool.compose()) {
        cserror() << "Couldn't compose block";
        return;
    }

    if (!pool.save()) {
        cserror() << "Couldn't save block";
        return;
    }

    {
        std::lock_guard<decltype(waiters_locker)> l(waiters_locker);
        new_block_cv.notify_all();
    }

    if (!updateCache(pool)) {
        cserror() << "Couldn't update cache";
    }
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
    return storage_.pool_load(ph);
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

csdb::PoolHash BlockChain::wait_for_block(const csdb::PoolHash& obsolete_block)
{
    std::unique_lock<decltype(dbLock_)> l(dbLock_);
    csdb::PoolHash res;

    new_block_cv.wait(l, [this, &obsolete_block, &res]() {

        res = storage_.last_hash();

        return obsolete_block != res;
    });

    return res;
}

csdb::Address BlockChain::getAddressFromKey(const std::string& key)
{
  std::string pk(static_cast<size_t>(PUBLIC_KEY_LENGTH), '\0');
    std::copy(key.rbegin(),
            std::min(key.rbegin() + PUBLIC_KEY_LENGTH, key.rend()),
              pk.rbegin());

    return csdb::Address::from_public_key(pk.data());
}

csdb::Amount BlockChain::getBalance(const csdb::Address& address) const
{
  {
    std::lock_guard<decltype(cacheMutex_)> lock(cacheMutex_);

    const WalletsCache::WalletData* walData =
        walletsCache_->findWallet(address.public_key());

    if (walData) {
      return walData->balance_;
    }
  }

  return calcBalance(address);
}

csdb::Amount BlockChain::calcBalance(const csdb::Address& address) const
{
  csdb::Amount result(0);
  csdb::Pool curr = loadBlock(getLastHash());

  while (curr.is_valid()) {
    size_t transactions_count = curr.transactions_count();

    for (size_t i = 0; i < transactions_count; ++i) {
      csdb::Transaction tr = curr.transaction(i);

      if (tr.source() == address) {
        result -= tr.amount();
      } else if (tr.target() == address) {
        result += tr.amount();
      }
    }

    curr = loadBlock(curr.previous_hash());
  }

  return result;
}

void BlockChain::onBlockReceived(csdb::Pool& pool)
{
  cslog() << "---------------------------  Write New Block: " << pool.sequence() << " :  " << pool.transactions_count() << " transactions"<<" --------------------------------";

  csdebug() << "sequence: " << pool.sequence() << ", time: " << pool.user_field(0).value<std::string>().c_str();
  csdebug() << " Last      hash: " << lastHash_.to_string();
  cslog() << pool.hash().to_string();
  csdebug() << "Checking Sequence ... ";

  if (pool.sequence() == getLastWrittenSequence() + 1) {
    pool.set_previous_hash(lastHash_);

    std::ofstream f(dbs_fname, std::ios::out);

    ht.head = 0;
    ht.tag = getLastWrittenSequence() + 1;

    bool file_is = f.is_open();

    if (file_is)
    {

      f << ht.head << "->" << ht.tag << std::endl;

      csdebug() << "DB structure: " << ht.head << "->" << ht.tag;
      csdebug() << "DB structure is written succesfully";

      f.close();
    }
    else {
      csdebug() << "Error writing DB structure";
    }

    writeBlock(pool);

    lastHash_ = pool.hash();
    blockHashes_.push_back(lastHash_);

    setLastWrittenSequence(pool.sequence());

    csdebug() << "New last hash: " << lastHash_.to_string();
    csdebug() << "New last storage size: " << storage_.size();

    if (global_sequence == getLastWrittenSequence()) {
      blockRequestIsNeeded = false;
    }

    return;
  }

  ////////////////////////////////////////////////////////////////////////////////////////////// Syncro!!!
  global_sequence = pool.sequence();
  blockRequestIsNeeded = true;
}

const csdb::PoolHash & BlockChain::getLastWrittenHash() const
{
  return lastHash_;
}

csdb::Storage & BlockChain::getStorage()
{
  return storage_;
}

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

namespace {
class TrxLoader {
 public:
  using Transaction = std::vector<csdb::Transaction>;

 public:
  TrxLoader(csdb::Address addr, const BlockChain& blockchain,
            Transaction& transactions)
      : addr_(addr), blockchain_(blockchain), transactions_(transactions) {}

  bool load(const csdb::PoolHash& poolHash, int64_t& offset, int64_t limit,
            csdb::PoolHash& prevPoolHash) {
    csdb::Pool curr = blockchain_.loadBlock(poolHash);
    if (!curr.is_valid()) {
      return false;
    }

    if (curr.transactions_count()) {
      auto curIdx = static_cast<csdb::TransactionID::sequence_t>(
          curr.transactions_count() - 1);

      while (true) {
        auto trans = curr.transaction(curIdx);

        if (trans.target() == addr_ || trans.source() == addr_) {
          if (offset == 0) {
            transactions_.push_back(trans);
          } else {
            --offset;
          }
        }

        if (transactions_.size() == limit) {
          break;
        }

        if (curIdx == 0) {
          break;
        }

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
}  // namespace

void BlockChain::getTransactions(Transactions& transactions,
                                 csdb::Address& address, int64_t offset,
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
         i != std::numeric_limits<decltype(i)>::max(); --i) {
      const auto& poolHashData = hashesArray[i];

      if (poolHashData.trxNum <
              WalletsCache::WalletData::PoolHashData::maxTrxNum &&
          poolHashData.trxNum <= offset) {
        offset -= poolHashData.trxNum;
        continue;
      }

      csdb::PoolHash currHash;
      WalletsCache::convert(poolHashData.poolHash, currHash);

      if (!trxLoader.load(currHash, offset, limit, prevHash)) return;
      if (transactions.size() >= size_t(limit)) return;
    }
  }

  while (true) {
    csdb::PoolHash currHash = prevHash;
    if (!trxLoader.load(currHash, offset, limit, prevHash)) break;
  }
}
