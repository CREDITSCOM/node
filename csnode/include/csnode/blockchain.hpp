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

#include <csdb/address.hpp>
#include <csdb/amount.hpp>
#include <csdb/amount_commission.hpp>
#include <csdb/pool.hpp>
#include <csdb/storage.hpp>
#include <csdb/internal/types.hpp>

#include <csnode/nodecore.hpp>
#include <csnode/multiwallets.hpp>
#include <csnode/walletsids.hpp>
#include <csnode/poolcache.hpp>
#include <csnode/caches_serialization_manager.hpp>

#include <roundpackage.hpp>

#include <lib/system/concurrent.hpp>

#include <condition_variable>
#include <mutex>

#include <lib/system/common.hpp>

namespace cs {
class BlockHashes;
class WalletsIds;
class Fee;
class TransactionsIndex;
class TransactionsPacket;
class BlockChain_Serializer;

/** @brief   The synchronized block signal emits when block is trying to be stored */
using TryToStoreBlockSignal = cs::Signal<void(const csdb::Pool&, bool*)>;
using NeedStopSignal = cs::Signal<void(bool)>;
/** @brief   The new block signal emits when finalizeBlock() occurs just before recordBlock() */
using StoreBlockSignal = cs::Signal<void(const csdb::Pool&)>;
using OrderNecessaryBlockSignal = cs::Signal<void(csdb::PoolHash, cs::Sequence)>;

/** @brief   The write block or remove block signal emits when block is flushed to disk */
using ChangeBlockSignal = cs::Signal<void(const cs::Sequence)>;
using RemoveBlockSignal = cs::Signal<void(const csdb::Pool&)>;
using AlarmSignal = cs::Signal<void(const cs::Sequence)>;
using ReadBlockSignal = csdb::ReadBlockSignal;
using StartReadingBlocksSignal = csdb::BlockReadingStartedSingal;
using StopReadingBlocksSignal = cs::Signal<void(uint64_t, bool)>;
using SuccessQSSignal = cs::Signal<void(csdb::Amount, csdb::Amount, bool, bool, uint32_t)> ;
}  // namespace cs

class BlockChain {
public:
    using Transactions = std::vector<csdb::Transaction>;
    using WalletId = csdb::internal::WalletId;
    using WalletAddress = csdb::Address;
    using WalletData = cs::WalletsCache::WalletData;

    enum class AddressType {
        PublicKey,
        Id
    };

    explicit BlockChain(csdb::Address genesisAddress, csdb::Address startAddress,
                        bool recreateIndex = false);
    ~BlockChain();

    bool init(
      const std::string& path,
      cs::CachesSerializationManager*,
      std::set<cs::PublicKey>& initialConfidants,
      cs::Sequence newBlockchainTop = cs::kWrongSequence
    );
    // called immediately after object construction, better place to subscribe on signals
    void subscribeToSignals();

    bool isGood() const;

    // return unique id of database if at least one unique block has written, otherwise (only genesis block) 0
    uint64_t uuid() const;

    // utility methods

    csdb::Address getAddressByType(const csdb::Address& addr, AddressType type) const;
    bool isEqual(const csdb::Address& laddr, const csdb::Address& raddr) const;

    static csdb::Address getAddressFromKey(const std::string&);
    static csdb::Address getAddressFromKey(const cs::Bytes&);

    static uint64_t getBlockTime(const csdb::Pool& block) noexcept;
    static std::string poolInfo(const csdb::Pool& pool);
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

    bool storeBlock(csdb::Pool& pool, cs::PoolStoreType type);

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

    std::optional<csdb::Pool> createBlock(csdb::Pool& pool) {
        return recordBlock(pool, true);
    }

    void removeWalletsInPoolFromCache(const csdb::Pool& pool);
    void removeLastBlock();
    void showDBParams();
    /**
     * Mark last block as compromised and handle the situation:
     *  - store required parameters  
     *  - make a request for proper block variant
     *
     * @author  Alexander Avramenko
     * @date    31.01.2020
     *
     * @param   desired_hash    The desired hash of last block to request.
     *
     * @returns True if it succeeds, false if it fails.
     */

    bool compromiseLastBlock(const csdb::PoolHash& desired_hash);

    // updates fees in every transaction
    void setTransactionsFees(cs::TransactionsPacket& packet);
    void setTransactionsFees(csdb::Pool& pool, cs::PoolStoreType type = cs::PoolStoreType::Created);
    void setTransactionsFees(std::vector<csdb::Transaction>& transactions);
    void setTransactionsFees(std::vector<csdb::Transaction>& transactions, const cs::Bytes& characteristicMask);

    bool addNewWalletsToPool(csdb::Pool& pool);
    void updateLastTransactions(const std::vector<std::pair<cs::PublicKey, csdb::TransactionID>>&);

    bool checkForConsistency(csdb::Pool & pool, bool isNew);

    // storage adaptor
    void close();
    bool getTransaction(const csdb::Address& addr, const int64_t& innerId, csdb::Transaction& result) const;
    void arrangeBlocksInCache();


public:
    std::string getLastTimeStamp() const;
    cs::Bytes getLastRealTrusted() const;
    bool updateLastBlock(cs::RoundPackage& rPackage);
    bool updateLastBlock(cs::RoundPackage& rPackage, const csdb::Pool& poolFrom);
    bool deferredBlockExchange(cs::RoundPackage& rPackage, const csdb::Pool& newPool);
    bool isSpecial(const csdb::Transaction& t);
    cs::Bytes checkForSpecialTransactions(const std::vector<csdb::Transaction>& trxs, cs::Sequence seq);
    cs::Sequence getLastSeq() const;

    static inline const csdb::user_field_id_t kFieldTimestamp = 0;
    static inline const csdb::user_field_id_t kFieldServiceInfo = 1;
    static inline const csdb::user_field_id_t kFieldBlockReward = 3;

    static void setTimestamp(csdb::Pool& block, const std::string& timestamp);
    static void setBootstrap(csdb::Pool& block, bool is_bootstrap);
    static bool isBootstrap(const csdb::Pool& block);

    const cs::MultiWallets& multiWallets() const;

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
    std::size_t getCachedBlocksSizeSynced() const;
    void clearBlockCache();

    void cacheLastBlocks();
    void replaceCachedIncorrectBlock(const csdb::Pool& block);
    void getCachedMissedBlock(const csdb::Pool& block);
    void badBlockIssue(const csdb::Pool& pool);
    void lookForBadBlocks();

    std::vector<cs::Sequence>* getIncorrectBlockNumbers();
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
    std::optional<SequenceInterval> getFreeSpaceBlocks() const;

    bool isLastBlockUncertain() const {
        return uncertainLastBlockFlag_;
    }

public signals:
    //orderNecessaryBlock(&tryBlock, lastBlock.sequence());
    cs::OrderNecessaryBlockSignal orderNecessaryBlock;

    /** @brief The new block event. Raised when the next incoming block is finalized and just before stored into chain */
    cs::StoreBlockSignal storeBlockEvent;

    cs::NeedStopSignal stopNode;

    /** @brief The event storing synchronized block. Raised when the next incoming block is trying to be stored into chain */
    cs::TryToStoreBlockSignal tryToStoreBlockEvent;

    /** @brief The cached block event. Raised when the next block is flushed to storage */
    cs::ChangeBlockSignal cachedBlockEvent;

    /** @brief The remove block event. Raised when the next block is flushed to storage */
    cs::RemoveBlockSignal removeBlockEvent;

    /** @brief Alarm event. Block Isn't correct */
    cs::AlarmSignal alarmBadBlock;

    /** @brief Alarm event. Uncertain that last block is valid */
    cs::AlarmSignal uncertainBlock;

    const cs::ReadBlockSignal& readBlockEvent() const;
    const cs::StartReadingBlocksSignal& startReadingBlocksEvent() const;

    cs::StopReadingBlocksSignal stopReadingBlocksEvent;
    cs::SuccessQSSignal successfullQuickStartEvent;

public slots:

    // subscription is placed in SmartContracts constructor
    void onPayableContractReplenish(const csdb::Transaction& starter) {
        this->walletsCacheUpdater_->invokeReplenishPayableContract(starter, false /*inverse*/);
    }
    void onContractTimeout(const csdb::Transaction& starter) {
        this->walletsCacheUpdater_->rollbackExceededTimeoutContract(starter, csdb::Amount(0), false /*inverse*/);
    }
    void onContractEmittedAccepted(const csdb::Transaction& emitted, const csdb::Transaction& starter) {
        this->walletsCacheUpdater_->smartSourceTransactionReleased(emitted, starter, false /*inverse*/);
    }
    void rollbackPayableContractReplenish(const csdb::Transaction& starter) {
        this->walletsCacheUpdater_->invokeReplenishPayableContract(starter, true /*inverse*/);
    }
    void rollbackContractTimeout(const csdb::Transaction& starter) {
        this->walletsCacheUpdater_->rollbackExceededTimeoutContract(starter, csdb::Amount(0), true /*inverse*/);
    }
    void rollbackContractEmittedAccepted(const csdb::Transaction& emitted, const csdb::Transaction& starter) {
        this->walletsCacheUpdater_->smartSourceTransactionReleased(emitted, starter, true /*inverse*/);
    }

public:

    // load methods

    csdb::Pool loadBlock(const csdb::PoolHash&) const;
    csdb::Pool loadBlock(const cs::Sequence sequence) const;
    csdb::Pool loadBlockForSync(const cs::Sequence sequence) const;
    csdb::Pool loadBlockMeta(const csdb::PoolHash&, size_t& cnt) const;
    csdb::Transaction loadTransaction(const csdb::TransactionID&) const;
    void iterateOverWallets(const std::function<bool(const cs::PublicKey&, const cs::WalletsCache::WalletData&)>);
    csdb::Pool getLastBlock() const {
        return loadBlock(getLastSeq());
    }

    bool isAntiForkModeOn() {
        return antiForkMode_;
    }
    // info

    size_t getSize() const;
    uint64_t getWalletsCountWithBalance();
    uint64_t getWalletsCount() const;
    csdb::PoolHash getLastHash() const;
    csdb::PoolHash getHashBySequence(cs::Sequence seq) const;
    cs::Sequence getSequenceByHash(const csdb::PoolHash&) const;

    // get inner data (from caches)

    bool findWalletData(const csdb::Address&, WalletData& wallData, WalletId& id) const;
    bool findWalletData(WalletId id, WalletData& wallData) const;
    bool findWalletData(const csdb::Address&, WalletData& wallData) const;
    bool findWalletId(const WalletAddress& address, WalletId& id) const;
    // wallet transactions: pools cache + db search
    void getTransactions(Transactions& transactions, csdb::Address address, uint64_t offset, uint64_t limit);
    void getTransactionsUntill(Transactions& transactions, csdb::Address address, csdb::TransactionID id, uint16_t flagg);
    void getAccountRegTime(uint64_t& aTime, csdb::Address address);
    void setBlocksToBeRemoved(cs::Sequence number);
    double getStakingCoefficient(cs::StakingCoefficient coeff);

    std::string printWalletCaches();


#ifdef MONITOR_NODE
    void iterateOverWriters(const std::function<bool(const cs::PublicKey&, const cs::WalletsCache::TrustedData&)>);
    void applyToWallet(const csdb::Address&, const std::function<void(const cs::WalletsCache::WalletData&)>); 
#endif
    uint32_t getTransactionsCount(const csdb::Address&);

    csdb::TransactionID getLastTransaction(const csdb::Address&) const;
    cs::Sequence getPreviousPoolSeq(const csdb::Address&, cs::Sequence) const;

    std::pair<cs::Sequence, uint32_t> getLastNonEmptyBlock();
    std::pair<cs::Sequence, uint32_t> getPreviousNonEmptyBlock(cs::Sequence);
    uint64_t getTransactionsCount() const {
        return totalTransactionsCount_;
    }

    const csdb::Address& getGenesisAddress() const;

    bool updateContractData(const csdb::Address& abs_addr, const cs::Bytes& data) const;
    bool getContractData(const csdb::Address& abs_addr, cs::Bytes& data) const;

    const cs::WalletsCache::Updater& getCacheUpdater() const {
        return *(walletsCacheUpdater_.get());
    }

    /**
     * Try to flush deferred block to DB, intended to call on node exit
     *
     * @author  Alexander Avramenko
     * @date    25.09.2019
     */

    void tryFlushDeferredBlock();
    void addIncorrectBlockNumber(cs::Sequence seq);

    csdb::Amount getBlockReward() {
        return csdb::Amount(blockRewardIntegral_, blockRewardFraction_);
    }

    csdb::Amount getMiningCoefficient() {
        return csdb::Amount(miningCoefficientIntegral_, miningCoefficientFraction_);
    }

    bool getStakingOn() {
        return stakingOn_;
    }

    bool getMiningOn() {
        return miningOn_;
    }

    uint32_t getTimeMinStage1() {
        return TimeMinStage1_;
    }

    void setBlockReward(csdb::Amount reward);
    void setMiningCoefficient(csdb::Amount coefficient);
    void setMiningOn(bool mOn);
    void setStakingOn(bool stOn);
    void setTimeMinStage1(uint32_t timeStage1);


private:
    void createCachesPath();
    bool findAddrByWalletId(const WalletId id, csdb::Address& addr) const;
    void writeGenesisBlock();

    void logBlockInfo(csdb::Pool& pool);

    // Thread unsafe
    bool finalizeBlock(csdb::Pool& pool, bool isTrusted, cs::PublicKeys lastConfidants);
    bool applyBlockToCaches(const csdb::Pool& pool);

    void onStartReadFromDB(cs::Sequence lastWrittenPoolSeq);
    void onReadFromDB(csdb::Pool block, bool* shouldStop);
    bool postInitFromDB(bool successfulQuickStart);

    bool updateWalletIds(const csdb::Pool& pool, cs::WalletsCache::Updater& updater);
    bool insertNewWalletId(const csdb::Address& newWallAddress, WalletId newWalletId, cs::WalletsCache::Updater& updater);

    bool findWalletData_Unsafe(WalletId id, WalletData& wallData) const;

    class TransactionsLoader;

    void updateNonEmptyBlocks(const csdb::Pool&);

    bool good_;

    mutable std::recursive_mutex dbLock_;
    csdb::Storage storage_;

    std::unique_ptr<cs::BlockHashes> blockHashes_;
    std::unique_ptr<cs::TransactionsIndex> trxIndex_;

    const csdb::Address genesisAddress_;
    const csdb::Address startAddress_;

    std::unique_ptr<cs::WalletsIds> walletIds_;
    std::unique_ptr<cs::WalletsCache> walletsCacheStorage_;
    std::unique_ptr<cs::WalletsCache::Updater> walletsCacheUpdater_;

    mutable cs::SpinLock cacheMutex_{ATOMIC_FLAG_INIT};

    uint64_t totalTransactionsCount_ = 0;

    struct NonEmptyBlockData {
        cs::Sequence poolSeq;
        uint32_t transCount = 0;
    };

    std::map<cs::Sequence, NonEmptyBlockData> previousNonEmpty_;
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
        cs::PoolStoreType type;
    };

    mutable std::mutex cachedBlocksMutex_;
    std::unique_ptr<cs::PoolCache> cachedBlocks_;

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

    // may be modified once in uuid() method:
    mutable std::atomic<uint64_t> uuid_ = 0;
    std::atomic<cs::Sequence> lastSequence_;
    cs::Sequence blocksToBeRemoved_ = 0;
    std::atomic_bool stop_ = false;

    std::vector<cs::Sequence> incorrectBlocks_;//blocks with wrong hashes (during db reading)
    std::vector<cs::Sequence> badBlocks_;//blocks containing invalid transactions
    bool selectionFinished_ = true;
    csdb::PoolHash lastPrevHash_ = csdb::PoolHash();
    bool antiForkMode_ = false;
    uint8_t emittingRequest_ = 0;
    cs::Sequence neededCacheSeq_ = 0ULL;
    cs::Sequence startingBchSeq_ = 0ULL;


    // support the ability to replace last deferred block by the alternative with the same content, anti-fork feature
    // flag the last block is uncertain:
    bool uncertainLastBlockFlag_ = false;
    // sequence of uncertain block, if uncertainLastBlock_ == true
    cs::Sequence uncertainSequence_ = 0;
    // hash of uncertain block
    csdb::PoolHash uncertainHash_;
    // desired hash of block with the same (uncertain) sequence and the same content as the current (uncertain) block:
    csdb::PoolHash desiredHash_;
    // counter of successfully replaced uncertain blocks
    size_t cntUncertainReplaced = 0;

    void resetUncertainState() {
        uncertainLastBlockFlag_ = false;
        uncertainSequence_ = 0;
        uncertainHash_ = csdb::PoolHash{};
        desiredHash_ = csdb::PoolHash{};
    }

    bool tryQuickStart(cs::CachesSerializationManager*, std::set<cs::PublicKey>& initialConfidants);
    bool bindSerializationManToCaches(cs::CachesSerializationManager*, std::set<cs::PublicKey>& initialConfidants);

    cs::CachesSerializationManager* serializationManPtr_ = nullptr;

    // compare only state content: transactions, new wallets, sequence, round fee, user fields
    // true if both pools are not valid, or both pools have equal state content
    static bool testContentEqual(const csdb::Pool& lhs, const csdb::Pool& rhs);

    friend class cs::BlockChain_Serializer;

    const size_t kQuickStartSaveCachesInterval = 10'000'000;
    int32_t blockRewardIntegral_;
    uint64_t blockRewardFraction_ ;
    int32_t miningCoefficientIntegral_;
    uint64_t miningCoefficientFraction_;
    bool stakingOn_;
    bool miningOn_;
    uint32_t TimeMinStage1_;
};
#endif  //  BLOCKCHAIN_HPP
