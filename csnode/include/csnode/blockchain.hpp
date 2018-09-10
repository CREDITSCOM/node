#pragma once

#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include <fstream>
#include <iostream>
#include <string>

#include <boost/dynamic_bitset.hpp>

#include <csdb/address.h>
#include <csdb/amount.h>
#include <csdb/pool.h>
#include <csdb/storage.h>

#include <csdb/internal/types.h>
#include <csnode/threading.hpp>
#include <csnode/WalletsIds.h>
#include <csnode/WalletsCache.h>
#include <csnode/WalletsPools.h>

#include <condition_variable>
#include <mutex>

namespace Credits
{
    class WalletsIds;
}

class BlockChain
{

  struct Headtag
  {
    uint32_t head;
    uint32_t tag;
  };

public:
  std::string dbs_fname;
  using Transactions = std::vector<csdb::Transaction>;
  using WalletId = csdb::internal::WalletId;
  using WalletAddress = csdb::Address;
  using WalletData = Credits::WalletsCache::WalletData;
  using Mask = boost::dynamic_bitset<uint64_t>;

  BlockChain(const std::string& path, csdb::Address genesisAddress, csdb::Address startAddress);

  bool isGood() const { return good_; }

  void finishNewBlock(csdb::Pool& pool);
  void writeNewBlock(csdb::Pool& pool);
  void onBlockReceived(csdb::Pool& pool);

  size_t getSize() const;
  csdb::PoolHash getLastHash() const;

  csdb::PoolHash wait_for_block(const csdb::PoolHash& obsolete);

  csdb::Pool loadBlock(const csdb::PoolHash&) const;
  csdb::Pool loadBlockMeta(const csdb::PoolHash&, size_t& cnt) const;
  csdb::Transaction loadTransaction(const csdb::TransactionID&) const;

  static csdb::Address getAddressFromKey(const std::string&);

  void setLastWrittenSequence(uint32_t seq);
  uint32_t getLastWrittenSequence() const;

  uint32_t getRequestedBlockNumber() const;

  void setGlobalSequence(uint32_t seq);
  uint32_t getGlobalSequence() const;

  bool getBlockRequestNeed() const;

  csdb::PoolHash getHashBySequence(uint32_t seq) const;
  csdb::PoolHash getLastWrittenHash() const;

  // slightly faster than findWalletData
  csdb::Amount getBalance(const csdb::Address&) const;
  csdb::Amount getBalance(const WalletId&) const;
 
  // all wallet data (from cache)
  bool findWalletData(const csdb::Address&, WalletData& wallData) const;
  bool findWalletData(const WalletId&, WalletData& wallData) const;

  // wallet transactions: pools cache + db search
  void getTransactions(
      Transactions& transactions,
      csdb::Address address,
      uint64_t offset,
      uint64_t limit) const;

  // wallets modified by last new block
  bool getModifiedWallets(Mask& dest) const;

  // wallet id interface
 
  // returns false if wallet address already existed
  bool insertWalletId(const WalletAddress& address, WalletId id);
  // searches for existing wallet id
  // returns true if found
  bool findWalletId(const WalletAddress& address, WalletId& id) const;
  // finds existing or creates new wallet id
  // returns true if new wallet id was created
  bool getWalletId(const WalletAddress& address, WalletId& id);

private:
  bool initFromDB(const std::string& path, bool& wasJustCreated);

  Headtag ht;
  void writeGenesisBlock();
  void putBlock(csdb::Pool& pool);
  void writeBlock(csdb::Pool& pool);

  bool initCaches();

  bool updateWalletIds(const csdb::Pool& pool);
  bool updateWalletsCache(csdb::Pool& pool);
  bool updateWalletsPools(csdb::Pool& pool);
  void addNewWalletsToPool(csdb::Pool& pool);
  void addNewWalletToPool(const csdb::Address& walletAddress, const csdb::Pool::NewWalletInfo::AddressId& addressId, csdb::Pool::NewWallets& newWallets);

  bool findWalletData_Unsafe(const WalletId& id, WalletData& wallData) const;
  csdb::Amount getBalance_Unsafe(const WalletId& id) const;

  bool findDataForTransactions(
      csdb::Address address,
      csdb::Address& wallPubKey,
      Credits::WalletsPools::WalletData::PoolsHashes& hashesArray) const;

  void getTransactions(
      Transactions & transactions,
      csdb::Address wallPubKey,
      const Credits::WalletsPools::WalletData::PoolsHashes& hashesArray,
      uint64_t offset,
      uint64_t limit) const;

private:
  bool good_;

  mutable Credits::spinlock dbLock_;
  csdb::Storage storage_;

  csdb::PoolHash lastHash_;
  uint32_t last_written_sequence;
  uint32_t global_sequence;
  bool blockRequestIsNeeded;

  std::vector<csdb::PoolHash> blockHashes_;

  const csdb::Address genesisAddress_;
  const csdb::Address startAddress_;
  std::unique_ptr<Credits::WalletsIds> walletIds_;
  std::unique_ptr<Credits::WalletsCache> walletsCache_;
  std::unique_ptr<Credits::WalletsPools> walletsPools_;
  mutable Credits::spinlock cacheMutex_;

  std::condition_variable_any new_block_cv;
  Credits::spinlock waiters_locker;
};