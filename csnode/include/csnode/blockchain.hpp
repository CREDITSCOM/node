#ifndef BLOCKCHAIN_HPP
#define BLOCKCHAIN_HPP

#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include <fstream>
#include <iostream>
#include <string>

#include <boost/dynamic_bitset.hpp>

#include <csdb/address.hpp>
#include <csdb/amount.hpp>
#include <csdb/amount_commission.hpp>
#include <csdb/pool.hpp>
#include <csdb/storage.hpp>

#include <csdb/internal/types.hpp>
#include <csnode/walletscache.hpp>
#include <csnode/walletsids.hpp>
#include <csnode/walletspools.hpp>
#include <csnode/threading.hpp>
#include <csnode/nodecore.hpp>

#include <lib/system/signals.hpp>

#include <condition_variable>
#include <mutex>

namespace cs {
class BlockHashes;
class WalletsIds;
class Fee;
class TransactionsPacket;

using SmartContractStartSignal = cs::Signal<void(const csdb::Pool, size_t)>;
using WriteBlockSignal = cs::Signal<void(const cs::Sequence)>;
}  // namespace cs

class BlockChain {
public:
  using Transactions  = std::vector<csdb::Transaction>;
  using WalletId      = csdb::internal::WalletId;
  using WalletAddress = csdb::Address;
  using WalletData    = cs::WalletsCache::WalletData;
  using Mask          = boost::dynamic_bitset<uint64_t>;

  explicit BlockChain(const std::string& path, csdb::Address genesisAddress, csdb::Address startAddress);
  ~BlockChain();

  bool isGood() const;

  enum class ADDR_TYPE { PUBLIC_KEY, ID };
  csdb::Address get_addr_by_type(const csdb::Address &addr, ADDR_TYPE type) const;
  bool is_equal(const csdb::Address &laddr, const csdb::Address &raddr) const;

  /**
   * @fn    bool BlockChain::storeBlock(csdb::Pool pool, bool by_sync);
   *
   * @brief Stores a block
   *
   * @author    Alexander Avramenko
   * @date  23.11.2018
   *
   * @param pool    The pool representing block to store in blockchain. Its sequence number MUST be
   *                set. It will be modified.
   * @param by_sync False if block is new, just constructed, true if block is received via sync subsystem
   *
   * @return    True if it succeeds, false if it fails. True DOES NOT MEAN the block recorded to
   *            chain. It means block is correct and possibly recorded. If it is not recorded now, it is cached
   *            for future use and will be recorded on time
   */

  bool  storeBlock(csdb::Pool pool, bool by_sync);

  /**
   * @fn    std::optional<csdb::Pool> BlockChain::createBlock(csdb::Pool pool);
   *
   * @brief Creates a block and records to blockchain
   *
   * @author    Alexander Avramenko
   * @date  23.11.2018
   *
   * @param pool    The pool.
   *
   * @return    The new recorded block if ok, otherwise nullopt.
   */

  std::optional<csdb::Pool> createBlock(csdb::Pool pool);

private:
  void removeWalletsInPoolFromCache(const csdb::Pool& pool);  // obsolete?

public:
  size_t getSize() const;
  csdb::PoolHash getLastHash() const;

  csdb::PoolHash wait_for_block(const csdb::PoolHash& obsolete);

  csdb::Pool loadBlock(const csdb::PoolHash&) const;
  csdb::Pool loadBlock(const cs::Sequence sequence) const;
  csdb::Pool loadBlockMeta(const csdb::PoolHash&, size_t& cnt) const;
  csdb::Transaction loadTransaction(const csdb::TransactionID&) const;
  void removeLastBlock();

  static csdb::Address getAddressFromKey(const std::string&);
  csdb::internal::byte_array getKeyFromAddress(csdb::Address&) const;

  cs::Sequence getLastWrittenSequence() const;

  cs::Sequence getRequestedBlockNumber() const;

  void iterateOverWallets(const std::function<bool(const cs::WalletsCache::WalletData::Address&, const cs::WalletsCache::WalletData&)>);

#ifdef MONITOR_NODE
  void iterateOverWriters(const std::function<bool(const cs::WalletsCache::WalletData::Address&, const cs::WalletsCache::WriterData&)>);
  void applyToWallet(const csdb::Address&, const std::function<void(const cs::WalletsCache::WalletData&)>);
#endif

  uint64_t getWalletsCount();

  csdb::PoolHash getHashBySequence(cs::Sequence seq) const;
  csdb::PoolHash getLastWrittenHash() const;

#ifdef TRANSACTIONS_INDEX
  csdb::TransactionID getLastTransaction(const csdb::Address&);
  csdb::PoolHash getPreviousPoolHash(const csdb::Address&, const csdb::PoolHash&);

  std::pair<csdb::PoolHash, uint32_t> getLastNonEmptyBlock();
  std::pair<csdb::PoolHash, uint32_t> getPreviousNonEmptyBlock(const csdb::PoolHash&);

  uint64_t getTransactionsCount() const { return total_transactions_count_; }
#endif

#ifdef MONITOR_NODE
  uint32_t getTransactionsCount(const csdb::Address&);
#endif

  // all wallet data (from cache)
  bool findWalletData(const csdb::Address&, WalletData& wallData, WalletId& id) const;
  bool findWalletData(WalletId id, WalletData& wallData) const;
  bool findAddrByWalletId(const WalletId id, csdb::Address& addr) const;

  // searches for existing wallet id
  // returns true if found
  bool findWalletId(const WalletAddress& address, WalletId& id) const;

  // wallet transactions: pools cache + db search
  void getTransactions(Transactions& transactions, csdb::Address address, uint64_t offset, uint64_t limit);

  // wallets modified by last new block
  bool getModifiedWallets(Mask& dest) const;

  // updates fees in every transaction
  void setTransactionsFees(cs::TransactionsPacket& packet);

  void updateLastBlockConfidants(const ::std::vector<::std::vector<uint8_t>>& confidants);

  const csdb::Storage& getStorage() const;

  struct AddrTrnxCount {
    uint64_t sendCount;
    uint64_t recvCount;
    uint64_t total_trxns_count;
  };

  void recount_trxns(const std::optional<csdb::Pool>& new_pool);
  const AddrTrnxCount& get_trxns_count(const csdb::Address& addr);
  //std::vector<csdb::Transaction> genesisTrxns_;
private:

  void writeGenesisBlock();
#ifdef TRANSACTIONS_INDEX
  void createTransactionsIndex(csdb::Pool&);
#endif

  void flushBlockToDisk(csdb::Pool& pool);
  void logBlockInfo(csdb::Pool& pool);

  void finalizeBlock(csdb::Pool& pool);

  bool initFromDB(cs::WalletsCache::Initer& initer);

  template <typename WalletCacheProcessor>
  bool updateWalletIds(const csdb::Pool& pool, WalletCacheProcessor& proc);
  bool insertNewWalletId(const csdb::Address& newWallAddress, WalletId newWalletId, cs::WalletsCache::Initer& initer);
  bool insertNewWalletId(const csdb::Address& newWallAddress, WalletId newWalletId, cs::WalletsCache::Updater& updater);

  void addNewWalletsToPool(csdb::Pool& pool);
  void addNewWalletToPool(const csdb::Address& walletAddress, const csdb::Pool::NewWalletInfo::AddressId& addressId,
                          csdb::Pool::NewWallets& newWallets);

  bool updateFromNextBlock(csdb::Pool& pool);

  // returns true if new id was inserted
  bool getWalletId(const WalletAddress& address, WalletId& id);
  bool findWalletData_Unsafe(WalletId id, WalletData& wallData) const;

  class TransactionsLoader;

  bool findDataForTransactions(csdb::Address address, csdb::Address& wallPubKey, WalletId& id,
                               cs::WalletsPools::WalletData::PoolsHashes& hashesArray) const;

  void getTransactions(Transactions& transactions, csdb::Address wallPubKey, WalletId id,
                       const cs::WalletsPools::WalletData::PoolsHashes& hashesArray, uint64_t offset, uint64_t limit);

  bool good_;

  mutable cs::SpinLock dbLock_;
  csdb::Storage storage_;

  csdb::PoolHash lastHash_;

  std::unique_ptr<cs::BlockHashes> blockHashes_;

  const csdb::Address genesisAddress_;
  const csdb::Address startAddress_;
  std::unique_ptr<cs::WalletsIds> walletIds_;
  std::unique_ptr<cs::WalletsCache> walletsCacheStorage_;
  std::unique_ptr<cs::WalletsCache::Updater> walletsCacheUpdater_;
  std::unique_ptr<cs::WalletsPools> walletsPools_;
  mutable cs::SpinLock cacheMutex_;

  std::condition_variable_any newBlockCv_;
  cs::SpinLock waitersLocker_;
  std::map<csdb::Address, AddrTrnxCount> transactionsCount_;

#ifdef TRANSACTIONS_INDEX
  uint64_t total_transactions_count_ = 0;

  struct NonEmptyBlockData {
    csdb::PoolHash hash;
    uint32_t transCount = 0;
  };
  std::map<csdb::PoolHash, NonEmptyBlockData> previousNonEmpty_;

  NonEmptyBlockData lastNonEmptyBlock_;
#endif

  // block cache

public:

  /**
   * @fn    std::size_t BlockChain::getCachedBlocksSize() const;
   *
   * @brief Gets amount of cached blocks
   *
   * @author    Alexander Avramenko
   * @date  06.12.2018
   *
   * @return    The cached blocks amount.
   */

  std::size_t getCachedBlocksSize() const;

  // continuous interval from ... to
  using SequenceInterval = std::pair<cs::Sequence, cs::Sequence>;

  /**
   * @fn    std::vector<SequenceInterval> BlockChain::getReqiredBlocks() const;
   *
   * @brief Gets required blocks in form vector of intervals. Starts with last written block and view through all cached
   * ones. Each interval means [first..second] including bounds. Last interval ends with current round number
   *
   * @author    Alexander Avramenko
   * @date  23.11.2018
   *
   * @return    The required blocks in form vector of intervals
   */

  std::vector<SequenceInterval> getRequiredBlocks() const;

  /**
   * @fn    void BlockChain::testCachedBlocks();
   *
   * @brief Tests cached blocks: removes outdated, records actual until sequence interrupted
   *
   * @author    Alexander Avramenko
   * @date  23.11.2018
   */

  void testCachedBlocks();

public signals:

  /** @brief The "smart contract started" event. Raised when every special "start smart contract" transaction included in block and stored.
  *   Connected to SolverCore::gotStartSmartContract() method
  */
  cs::SmartContractStartSignal smartContractEvent_;

  cs::WriteBlockSignal writeBlockEvent;

private:

  /**
   * @fn    std::pair<bool, std::optional<csdb::Pool>> BlockChain::recordBlock(csdb::Pool pool, bool requireAddWallets, std::optional<cs::PrivateKey> writer_key);
   *
   * @brief Finish pool, sign it or test signature, then record block to chain
   *
   * @author    Alexander Avramenko
   * @date  23.11.2018
   *
   * @param pool                The pool to finish &amp; record to chain.
   * @param requireAddWallets   If set, addNewWalletsToPool() called, otherwise updateWalletIds().
   *
   * @return    A std::pair of bool (success or fail) and std::optional&lt;csdb::Pool&gt; (recorded
   *            pool)
   */

  std::pair<bool, std::optional<csdb::Pool>> recordBlock(csdb::Pool pool, bool requireAddWallets);

  // to store outrunning blocks until the time to insert them comes
  // stores pairs of <block, sender> sorted by sequence number
  struct BlockMeta
  {
    csdb::Pool pool;
    // indicates that block has got by sync, so it is checked & tested in other way than ordinary ones
    bool by_sync;
  };
  std::map<cs::Sequence, BlockMeta> cachedBlocks_;

  // block storage to defer storing it in blockchain until confirmation from other nodes got
  // (idea is it is more easy not to store block immediately then to revert it after storing)
  csdb::Pool deferredBlock_;

  // fee calculator
  std::unique_ptr<cs::Fee> fee_;
};

class TransactionsIterator {
public:
  TransactionsIterator(BlockChain&, const csdb::Address&);

  void next();
  bool isValid() const;

  const csdb::Pool& getPool() const { return lapoo_; }

  const csdb::Transaction& operator*() const { return *it_; }
  auto operator->() const { return it_; }

private:
#ifdef TRANSACTIONS_INDEX
  void setFromTransId(const csdb::TransactionID&);
#else
  void setFromHash(const csdb::PoolHash&);
#endif

  BlockChain& bc_;

  csdb::Address addr_;
  csdb::Pool lapoo_;
  std::vector<csdb::Transaction>::const_reverse_iterator it_;
};


#endif  //  BLOCKCHAIN_HPP
