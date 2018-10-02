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
    class BlockHashes;
    class WalletsIds;
}

class BlockChain
{
public:
  using Transactions = std::vector<csdb::Transaction>;
  using WalletId = csdb::internal::WalletId;
  using WalletAddress = csdb::Address;
  using WalletData = Credits::WalletsCache::WalletData;
  using Mask = boost::dynamic_bitset<uint64_t>;

  BlockChain(const std::string& path, csdb::Address genesisAddress, csdb::Address startAddress
#ifdef SPAMMER
    , csdb::Address spammerAddress
#endif
  );
  ~BlockChain();

  bool isGood() const { return good_; }

  bool finishNewBlock(csdb::Pool& pool);
  bool writeNewBlock(csdb::Pool& pool);
  bool onBlockReceived(csdb::Pool& pool);

  size_t getSize() const;
  csdb::PoolHash getLastHash() const;

  csdb::PoolHash wait_for_block(const csdb::PoolHash& obsolete);

  csdb::Pool loadBlock(const csdb::PoolHash&) const;
  csdb::Pool loadBlockMeta(const csdb::PoolHash&, size_t& cnt) const;
  csdb::Transaction loadTransaction(const csdb::TransactionID&) const;

  static csdb::Address getAddressFromKey(const std::string&);

  uint32_t getLastWrittenSequence() const;

  uint32_t getRequestedBlockNumber() const;

  void setGlobalSequence(uint32_t seq);
  uint32_t getGlobalSequence() const;

  bool getBlockRequestNeed() const;

  csdb::PoolHash getHashBySequence(uint32_t seq) const;
  csdb::PoolHash getLastWrittenHash() const;

  // all wallet data (from cache)
  bool findWalletData(const csdb::Address&, WalletData& wallData, WalletId& id) const;
  bool findWalletData(WalletId id, WalletData& wallData) const;

  // searches for existing wallet id
  // returns true if found
  bool findWalletId(const WalletAddress& address, WalletId& id) const;

  // wallet transactions: pools cache + db search
  void getTransactions(
      Transactions& transactions,
      csdb::Address address,
      uint64_t offset,
      uint64_t limit);

  // wallets modified by last new block
  bool getModifiedWallets(Mask& dest) const;
  bool putBlock(csdb::Pool& pool);
private:
  bool writeGenesisBlock();
  
  void writeBlock(csdb::Pool& pool);

  bool initFromDB(Credits::WalletsCache::Initer& initer);

  template<typename WalletCacheProcessor>
  bool updateWalletIds(const csdb::Pool& pool, WalletCacheProcessor& proc);
  bool insertNewWalletId(const csdb::Address& newWallAddress, WalletId newWalletId, Credits::WalletsCache::Initer& initer);
  bool insertNewWalletId(const csdb::Address& newWallAddress, WalletId newWalletId, Credits::WalletsCache::Updater& updater);

  void addNewWalletsToPool(csdb::Pool& pool);
  void addNewWalletToPool(const csdb::Address& walletAddress, const csdb::Pool::NewWalletInfo::AddressId& addressId, csdb::Pool::NewWallets& newWallets);

  bool updateFromNextBlock(csdb::Pool& pool);

  // returns true if new id was inserted
  bool getWalletId(const WalletAddress& address, WalletId& id);
  bool findWalletData_Unsafe(WalletId id, WalletData& wallData) const;

  class TrxLoader;
  bool findDataForTransactions(
      csdb::Address address,
      csdb::Address& wallPubKey,
      WalletId& id,
      Credits::WalletsPools::WalletData::PoolsHashes& hashesArray) const;

  void getTransactions(
      Transactions & transactions,
      csdb::Address wallPubKey,
      WalletId id,
      const Credits::WalletsPools::WalletData::PoolsHashes& hashesArray,
      uint64_t offset,
      uint64_t limit);

private:
  bool good_;

  mutable Credits::spinlock dbLock_;
  csdb::Storage storage_;

  csdb::PoolHash lastHash_;
  uint32_t global_sequence;
  bool blockRequestIsNeeded;

  std::unique_ptr<Credits::BlockHashes> blockHashes_;

  const csdb::Address genesisAddress_;
  const csdb::Address startAddress_;
#ifdef SPAMMER
  const csdb::Address spammerAddress_;
#endif
  std::unique_ptr<Credits::WalletsIds> walletIds_;
  std::unique_ptr<Credits::WalletsCache> walletsCacheStorage_;
  std::unique_ptr<Credits::WalletsCache::Updater> walletsCacheUpdater_;
  std::unique_ptr<Credits::WalletsPools> walletsPools_;
  mutable Credits::spinlock cacheMutex_;

  std::condition_variable_any new_block_cv;
  Credits::spinlock waiters_locker;
};
