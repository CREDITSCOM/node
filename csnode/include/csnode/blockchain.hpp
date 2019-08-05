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
#include <csnode/nodecore.hpp>
#include <csnode/walletscache.hpp>
#include <csnode/walletsids.hpp>
#include <csnode/walletspools.hpp>
#include <roundpackage.hpp>

#include <lib/system/concurrent.hpp>

#include <condition_variable>
#include <mutex>

namespace cs {
class BlockHashes;
class WalletsIds;
class Fee;
class TransactionsPacket;

/** @brief   The new block signal emits when finalizeBlock() occurs just before recordBlock() */
using StoreBlockSignal = cs::Signal<void(const csdb::Pool&)>;

/** @brief   The write block or remove block signal emits when block is flushed to disk */
using ChangeBlockSignal = cs::Signal<void(const cs::Sequence)>;
using ReadBlockSignal = csdb::ReadBlockSignal;
}  // namespace cs

class BlockChain {
public:
    using Transactions = std::vector<csdb::Transaction>;
    using WalletId = csdb::internal::WalletId;
    using WalletAddress = csdb::Address;
    using WalletData = cs::WalletsCache::WalletData;
    using Mask = boost::dynamic_bitset<uint64_t>;

    enum class AddressType {
        PublicKey,
        Id
    };

    explicit BlockChain(csdb::Address genesisAddress, csdb::Address startAddress);
    ~BlockChain();

    bool init(const std::string& path);
    bool isGood() const;

    // return unique id of database if at least one unique block has written, otherwise (only genesis block) 0
    uint64_t uuid() const;

    // utility methods

    csdb::Address getAddressByType(const csdb::Address& addr, AddressType type) const;
    bool isEqual(const csdb::Address& laddr, const csdb::Address& raddr) const;

    static csdb::Address getAddressFromKey(const std::string&);

    // create/save block and related methods

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
     * @param by_sync False if block is new, just constructed, true if block is received via sync subsystem.
     *                False - addNewWalletsToPool() called. If true updateWalletIds() called.
     *
     * @return    True if it succeeds, false if it fails. True DOES NOT MEAN the block recorded to
     *            chain. It means block is correct and possibly recorded. If it is not recorded now, it is cached
     *            for future use and will be recorded on time
     */

    bool storeBlock(csdb::Pool& pool, bool bySync);

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

    std::optional<csdb::Pool> createBlock(csdb::Pool pool) {
        return recordBlock(pool, true);
    }

    void removeWalletsInPoolFromCache(const csdb::Pool& pool);
    void removeLastBlock();

    // updates fees in every transaction
    void setTransactionsFees(cs::TransactionsPacket& packet);
    void setTransactionsFees(csdb::Pool& pool);
    void setTransactionsFees(std::vector<csdb::Transaction>& transactions);
    void setTransactionsFees(std::vector<csdb::Transaction>& transactions, const cs::Bytes& characteristicMask);

    void addNewWalletsToPool(csdb::Pool& pool);

    // storage adaptor
    void close();
    bool getTransaction(const csdb::Address& addr, const int64_t& innerId, csdb::Transaction& result) const;

public:
    std::string getLastTimeStamp() const;
    cs::Bytes getLastRealTrusted() const;
    bool updateLastBlock(cs::RoundPackage& rPackage);
    bool updateLastBlock(cs::RoundPackage& rPackage, const csdb::Pool& poolFrom);
    bool deferredBlockExchange(cs::RoundPackage& rPackage, const csdb::Pool& newPool);
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

    /** @brief The new block event. Raised when the next incoming block is finalized and just before stored into chain */
    cs::StoreBlockSignal storeBlockEvent;

    /** @brief The cached block event. Raised when the next block is flushed to storage */
    cs::ChangeBlockSignal cachedBlockEvent;

    /** @brief The remove block event. Raised when the next block is flushed to storage */
    cs::ChangeBlockSignal removeBlockEvent;

    const cs::ReadBlockSignal& readBlockEvent() const;

public slots:

    // prototype is void (csdb::Transaction)
    // subscription is placed in SmartContracts constructor
    void onPayableContractReplenish(const csdb::Transaction& starter) {
        this->walletsCacheUpdater_->invokeReplenishPayableContract(starter);
    }
    void onContractTimeout(const csdb::Transaction& starter, cs::Sequence sequence) {
        cs::WalletsCache::RefContractCall ref{ sequence, uint32_t(starter.id().index()) };
        this->walletsCacheUpdater_->rollbackExceededTimeoutContract(starter, ref);
    }
    void onContractEmittedAccepted(const csdb::Transaction& emitted, const csdb::Transaction& starter) {
        this->walletsCacheUpdater_->smartSourceTransactionReleased(emitted, starter);
    }

public:

    // load methods

    csdb::Pool loadBlock(const csdb::PoolHash&) const;
    csdb::Pool loadBlock(const cs::Sequence sequence) const;
    csdb::Pool loadBlockMeta(const csdb::PoolHash&, size_t& cnt) const;
    csdb::Transaction loadTransaction(const csdb::TransactionID&) const;
    void iterateOverWallets(const std::function<bool(const cs::WalletsCache::WalletData::Address&, const cs::WalletsCache::WalletData&)>);
    csdb::Pool getLastBlock() const {
        return loadBlock(getLastSequence());
    }

    // info

    size_t getSize() const;
    uint64_t getWalletsCountWithBalance();
    csdb::PoolHash getLastHash() const;
    cs::Sequence getLastSequence() const;
    csdb::PoolHash getHashBySequence(cs::Sequence seq) const;
    cs::Sequence getSequenceByHash(const csdb::PoolHash&) const;

    // get inner data (from caches)

    bool findWalletData(const csdb::Address&, WalletData& wallData, WalletId& id) const;
    bool findWalletData(WalletId id, WalletData& wallData) const;
    bool findWalletId(const WalletAddress& address, WalletId& id) const;
    // wallet transactions: pools cache + db search
    void getTransactions(Transactions& transactions, csdb::Address address, uint64_t offset, uint64_t limit);
    // wallets modified by last new block
    bool getModifiedWallets(Mask& dest) const;

#ifdef MONITOR_NODE
    void iterateOverWriters(const std::function<bool(const cs::WalletsCache::WalletData::Address&, const cs::WalletsCache::TrustedData&)>);
    void applyToWallet(const csdb::Address&, const std::function<void(const cs::WalletsCache::WalletData&)>); 
#endif
    uint32_t getTransactionsCount(const csdb::Address&);

    csdb::TransactionID getLastTransaction(const csdb::Address&) const;
    csdb::PoolHash getPreviousPoolHash(const csdb::Address&, const csdb::PoolHash&);

    std::pair<csdb::PoolHash, uint32_t> getLastNonEmptyBlock();
    std::pair<csdb::PoolHash, uint32_t> getPreviousNonEmptyBlock(const csdb::PoolHash&);
    uint64_t getTransactionsCount() const {
        return total_transactions_count_;
    }

    const csdb::Address& getGenesisAddress() const;

    bool updateContractData(const csdb::Address& abs_addr, const cs::Bytes& data) const;
    bool getContractData(const csdb::Address& abs_addr, cs::Bytes& data) const;

private:
    void createCashesPath();
    bool findAddrByWalletId(const WalletId id, csdb::Address& addr) const;

    void writeGenesisBlock();
    void createTransactionsIndex(csdb::Pool&);

    void logBlockInfo(csdb::Pool& pool);

    // Thread unsafe
    bool finalizeBlock(csdb::Pool& pool, bool isTrusted, cs::PublicKeys lastConfidants);

    void onReadFromDB(csdb::Pool block, bool* shouldStop);
    bool postInitFromDB();

    template <typename WalletCacheProcessor>
    bool updateWalletIds(const csdb::Pool& pool, WalletCacheProcessor& proc);
    bool insertNewWalletId(const csdb::Address& newWallAddress, WalletId newWalletId, cs::WalletsCache::Initer& initer);
    bool insertNewWalletId(const csdb::Address& newWallAddress, WalletId newWalletId, cs::WalletsCache::Updater& updater);

    void addNewWalletToPool(const csdb::Address& walletAddress, const csdb::Pool::NewWalletInfo::AddressId& addressId, csdb::Pool::NewWallets& newWallets);

    bool updateFromNextBlock(csdb::Pool& pool);

    // returns true if new id was inserted
    bool getWalletId(const WalletAddress& address, WalletId& id);
    bool findWalletData_Unsafe(WalletId id, WalletData& wallData) const;

    class TransactionsLoader;

    bool findDataForTransactions(csdb::Address address, csdb::Address& wallPubKey, WalletId& id, cs::WalletsPools::WalletData::PoolsHashes& hashesArray) const;

    void getTransactions(Transactions& transactions, csdb::Address wallPubKey, WalletId id, const cs::WalletsPools::WalletData::PoolsHashes& hashesArray, uint64_t offset,
                         uint64_t limit);

    bool good_;

    mutable std::recursive_mutex dbLock_;
    csdb::Storage storage_;

    std::unique_ptr<cs::BlockHashes> blockHashes_;

    const csdb::Address genesisAddress_;
    const csdb::Address startAddress_;
    std::unique_ptr<cs::WalletsIds> walletIds_;
    std::unique_ptr<cs::WalletsCache> walletsCacheStorage_;
    std::unique_ptr<cs::WalletsCache::Updater> walletsCacheUpdater_;
    std::unique_ptr<cs::WalletsPools> walletsPools_;
    mutable cs::SpinLock cacheMutex_{ATOMIC_FLAG_INIT};

    uint64_t total_transactions_count_ = 0;

    struct NonEmptyBlockData {
        csdb::PoolHash hash;
        uint32_t transCount = 0;
    };
    std::map<csdb::PoolHash, NonEmptyBlockData> previousNonEmpty_;

    NonEmptyBlockData lastNonEmptyBlock_;

    /**
     * @fn    std::optional<csdb::Pool> BlockChain::recordBlock(csdb::Pool pool, std::optional<cs::PrivateKey> writer_key);
     *
     * @brief Finish pool, sign it or test signature, then record block to chain
     *
     * @author    Alexander Avramenko
     * @date  23.11.2018
     *
     * @param pool    The pool to finish &amp; record to chain.
     *
     * @return    A std::pair of bool (success or fail) and std::optional&lt;csdb::Pool&gt; (recorded
     *            pool)
     */

    std::optional<csdb::Pool> recordBlock(csdb::Pool& pool, bool isTrusted);

    // to store outrunning blocks until the time to insert comes;
    // stores pairs of <sequence, metadata>
    struct BlockMeta {
        csdb::Pool pool;
        // indicates that block has got by sync, so it is checked & tested in other way than ordinary ones
        bool by_sync;
    };
    std::map<cs::Sequence, BlockMeta> cachedBlocks_;

    // block storage to defer storing it in blockchain until confirmation from other nodes got
    // (idea is it is more easy not to store block immediately then to revert it after storing)
    csdb::Pool deferredBlock_;

    uint64_t uuidFromHash(const csdb::PoolHash& h) const {
        if (!h.is_empty()) {
            return *reinterpret_cast<uint64_t*>(h.to_binary().data());
        }
        return 0;
    }

    uint64_t uuidFromBlock(const csdb::Pool& block) const {
        if (block.is_valid()) {
            return uuidFromHash(block.hash());
        }
        return 0;
    }

    //uint64_t initUuid() const;

    // may be modified once in uuid() method:
    mutable uint64_t uuid_ = 0;
};

class TransactionsIterator {
public:
    TransactionsIterator(BlockChain&, const csdb::Address&);

    void next();
    bool isValid() const;

    const csdb::Pool& getPool() const {
        return lapoo_;
    }

    const csdb::Transaction& operator*() const {
        return *it_;
    }
    auto operator-> () const {
        return it_;
    }

private:
    void setFromTransId(const csdb::TransactionID&);

    BlockChain& bc_;

    csdb::Address addr_;
    csdb::Pool lapoo_;
    std::vector<csdb::Transaction>::const_reverse_iterator it_;
};

#endif  //  BLOCKCHAIN_HPP
