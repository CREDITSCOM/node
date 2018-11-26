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

#include <csdb/address.h>
#include <csdb/amount.h>
#include <csdb/amount_commission.h>
#include <csdb/pool.h>
#include <csdb/storage.h>

#include <csdb/internal/types.h>
#include <csnode/walletscache.hpp>
#include <csnode/walletsids.hpp>
#include <csnode/walletspools.hpp>
#include <csnode/threading.hpp>
#include <csnode/nodecore.hpp>

#include <condition_variable>
#include <mutex>

namespace cs {
class BlockHashes;
class WalletsIds;
class Fee;
class TransactionsPacket;
}  // namespace cs

class BlockChain {
public:
  using Transactions = std::vector<csdb::Transaction>;
  using WalletId = csdb::internal::WalletId;
  using WalletAddress = csdb::Address;
  using WalletData = cs::WalletsCache::WalletData;
  using Mask = boost::dynamic_bitset<uint64_t>;

  explicit BlockChain(const std::string& path, csdb::Address genesisAddress, csdb::Address startAddress);
  ~BlockChain();

  bool isGood() const;

  /**
   * @fn    bool BlockChain::storeBlock(csdb::Pool pool, std::optional<cs::Signature> writer_signature = {});
   *
   * @brief Stores a block
   *
   * @author    Alexander Avramenko
   * @date  23.11.2018
   *
   * @param pool                The pool representing block to store in blockchain. Its sequence
   *                            number MUST be set. Its writer_public_key MUST be set. It will be
   *                            modified.
   * @param writer_signature    (Optional) The signature made by writer node recording to chain. If
   *                            not set, skip block finishing and simply store block.
   *
   * @return    True if it succeeds, false if it fails. True DOES NOT MEAN the block recorded to
   *            chain. It means block is correct and possibly recorded.
   */

  bool storeBlock(csdb::Pool pool, std::optional<cs::Signature> writer_signature = std::nullopt);

  /**
   * @fn    std::optional<csdb::Pool> BlockChain::createBlock(csdb::Pool pool, const cs::PrivateKey& writer_key);
   *
   * @brief Creates a block, then signs it with private key and records to blockchain if possible
   *
   * @author    Alexander Avramenko
   * @date  23.11.2018
   *
   * @param pool        The pool.
   * @param writer_key  The writer private key to sign block
   *
   * @return    The new block.
   */

  std::optional<csdb::Pool> createBlock(csdb::Pool pool, const cs::PrivateKey& writer_key);

private:
  bool finishNewBlock(csdb::Pool& pool);                      // obsolete?
  void removeWalletsInPoolFromCache(const csdb::Pool& pool);  // obsolete?
  bool writeNewBlock(csdb::Pool& pool);                       // obsolete
  bool onBlockReceived(csdb::Pool& pool);                     // obsolete

public:
  size_t getSize() const;
  csdb::PoolHash getLastHash() const;

  csdb::PoolHash wait_for_block(const csdb::PoolHash& obsolete);

  csdb::Pool loadBlock(const csdb::PoolHash&) const;
  csdb::Pool loadBlock(const uint32_t sequence) const;
  csdb::Pool loadBlockMeta(const csdb::PoolHash&, size_t& cnt) const;
  csdb::Transaction loadTransaction(const csdb::TransactionID&) const;
  void removeLastBlock();

  static csdb::Address getAddressFromKey(const std::string&);

  uint32_t getLastWrittenSequence() const;

  uint32_t getRequestedBlockNumber() const;

  void setGlobalSequence(uint32_t seq);
  csdb::Pool::sequence_t getGlobalSequence() const;

  bool getBlockRequestNeed() const;

  csdb::PoolHash getHashBySequence(uint32_t seq) const;
  csdb::PoolHash getLastWrittenHash() const;

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
private:
  bool putBlock(csdb::Pool& pool);

public:
  const csdb::Storage& getStorage() const;

  struct AddrTrnxCount {
    uint64_t sendCount;
    uint64_t recvCount;
  };

  void recount_trxns(const std::optional<csdb::Pool>& new_pool);
  const AddrTrnxCount& get_trxns_count(const csdb::Address& addr);

private:
  bool writeGenesisBlock();

  void writeBlock(csdb::Pool& pool);

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

private:
  bool good_;

  mutable cs::spinlock dbLock_;
  csdb::Storage storage_;

  csdb::PoolHash lastHash_;
  csdb::Pool::sequence_t globalSequence_;
  bool blockRequestIsNeeded_;

  std::unique_ptr<cs::BlockHashes> blockHashes_;

  const csdb::Address genesisAddress_;
  const csdb::Address startAddress_;
  std::unique_ptr<cs::WalletsIds> walletIds_;
  std::unique_ptr<cs::WalletsCache> walletsCacheStorage_;
  std::unique_ptr<cs::WalletsCache::Updater> walletsCacheUpdater_;
  std::unique_ptr<cs::WalletsPools> walletsPools_;
  mutable cs::spinlock cacheMutex_;

  std::condition_variable_any newBlockCv_;
  cs::spinlock waitersLocker_;
  std::map<csdb::Address, AddrTrnxCount> transactionsCount_;

  // block cache

public:
  /**
   * @fn    csdb::Pool::sequence_t BlockChain::getLastCachedSequence() const;
   *
   * @brief Gets the last cached sequence
   *
   * @author    Alexander Avramenko
   * @date  23.11.2018
   *
   * @return    The last cached sequence.
   */

  csdb::Pool::sequence_t getLastCachedSequence() const;

  /**
   * @fn    csdb::Pool::sequence_t BlockChain::getLastSequence() const
   *
   * @brief Gets the last sequence both cached & written
   *
   * @author    Alexander Avramenko
   * @date  23.11.2018
   *
   * @return    The last sequence.
   */

  csdb::Pool::sequence_t getLastSequence() const;

  // continuous interval from ... to
  using SequenceInterval = std::pair<csdb::Pool::sequence_t, csdb::Pool::sequence_t>;

  /**
   * @fn    std::vector<SequenceInterval> BlockChain::getReqiredBlocks() const;
   *
   * @brief Gets required blocks in form vector of intervals. Starts with last written block and view through all cached
   * ones. Each interval means [first..second] including bounds. Last interval ends with 0 meaning "until end"
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

private:
  /**
   * @fn    std::pair<bool, std::optional<csdb::Pool>> BlockChain::recordBlock(csdb::Pool pool,
   * std::optional<cs::Signature> writer_signature, std::optional<cs::PrivateKey> writer_key);
   *
   * @brief Finish pool, sign it or test signature, then record block to chain
   *
   * @author    Alexander Avramenko
   * @date  23.11.2018
   *
   * @param pool                The pool to finish & record to chain
   * @param writer_signature    (Optional) The writer signature. If set, test signature before record
   * @param writer_key          (Optional) The writer key. If set, sign pool before record
   *
   * @return    A std::pair of bool (success or fail) and std::optional&lt;csdb::Pool&gt; (recorded pool)
   */

  std::pair<bool, std::optional<csdb::Pool>> recordBlock(csdb::Pool pool, std::optional<cs::Signature> writer_signature,
                                                         std::optional<cs::PrivateKey> writer_key);

  // to store outrunning blocks until the time to insert them comes
  // stores pairs of <block, sender> sorted by sequence number
  std::map<csdb::Pool::sequence_t, cs::PoolSyncMeta> cachedBlocks_;
  cs::Signature emptySignature_;

  // fee calculator
  std::unique_ptr<cs::Fee> fee_;
};

#endif  //  BLOCKCHAIN_HPP
