#ifndef POOLSYNCHRONIZER_HPP
#define POOLSYNCHRONIZER_HPP

#include <csdb/pool.hpp>

#include <csnode/blockchain.hpp>
#include <csnode/nodecore.hpp>

#include <lib/system/timer.hpp>
#include <lib/system/signals.hpp>

#include <net/neighbourhood.hpp>

class Node;

namespace cs {
using PoolSynchronizerRequestSignal = cs::Signal<void(const cs::PublicKey& target, const PoolsRequestedSequences& sequences)>;

class PoolSynchronizer {
public:
    explicit PoolSynchronizer(BlockChain* blockChain);

    void sync(cs::RoundNumber roundNum, cs::RoundNumber difference = kRoundDifferentForSync);
    void syncLastPool();

    // syncro get functions
    void getBlockReply(cs::PoolsBlock&& poolsBlock);

    // syncro send functions
    void sendBlockRequest();

    bool isSyncroStarted() const;

    void syncTill(cs::Sequence finSeq, cs::PublicKey& source);
    void trySource(cs::Sequence finSeq, cs::PublicKey& source);
    cs::PublicKeys getNeededNeighbours(cs::Sequence seq);

    static const cs::RoundNumber kRoundDifferentForSync = cs::values::kDefaultMetaStorageMaxSize;
    static const size_t kFreeBlocksTimeoutMs = 10000;
    static const size_t kCachedBlocksLimit = 10000;

public signals:
    PoolSynchronizerRequestSignal sendRequest;

private slots:
    void onTimeOut();

    void onWriteBlock(const csdb::Pool& pool);
    void onWriteBlock(const cs::Sequence sequence);
    void onRemoveBlock(const csdb::Pool& pool);

public slots:
    void onStoreBlockTimeElapsed();
    void onPingReceived(cs::Sequence sequence, const cs::PublicKey& publicKey);
    void onNeighbourAdded(const cs::PublicKey& publicKey, cs::Sequence sequence);
    void onNeighbourRemoved(const cs::PublicKey& publicKey);

private:
    enum class CounterType;
    enum class SequenceRemovalAccuracy;
    class Neighbour;

    // pool sync progress
    bool showSyncronizationProgress(const cs::Sequence lastWrittenSequence) const;

    bool checkActivity();

    void sendBlock(const Neighbour& neighbour);
    void sendBlock(const Neighbour& neighbour, const PoolsRequestedSequences& sequeces);

    bool getNeededSequences(Neighbour& neighbour);

    void checkNeighbourSequence(const cs::Sequence sequence, const SequenceRemovalAccuracy accuracy);
    void removeExistingSequence(const cs::Sequence sequence, const SequenceRemovalAccuracy accuracy);

    template<typename T>
    void printFreeBlocks(const T& key, const PoolsRequestedSequences& sequeces);

    bool isLastRequest() const;

    // neighbours private interfaces
    bool isAddableNeighbour(cs::Sequence sequence) const;
    bool isNeighbourExists(const cs::PublicKey& key) const;
    bool isNeighbourExists(const Neighbour& neighbour) const;
    Neighbour& addNeighbour(const cs::PublicKey& key);
    Neighbour& addNeighbour(const Neighbour& neighbour);
    Neighbour& getNeighbour(const cs::PublicKey& key);
    Neighbour& getNeighbour(const Neighbour& element);
    cs::Sequence neighboursMaxSequence() const;

    void checkCachedBlocks();
    void synchroFinished();
    size_t nextIndex(size_t index) const;

private:
    enum class SequenceRemovalAccuracy {
        Exact,
        LowerBound,
        UpperBound
    };

    class Neighbour {
    public:
        Neighbour() = default;

        explicit Neighbour(const cs::PublicKey& publicKey)
        : key_(publicKey) {
        }

        explicit Neighbour(const cs::PublicKey& publicKey, cs::Sequence blockPoolsCount)
        : key_(publicKey) {
            sequences_.reserve(blockPoolsCount);
        }

        inline bool removeSequnce(const cs::Sequence sequence, const SequenceRemovalAccuracy accuracy) {
            if (sequences_.empty()) {
                return false;
            }

            bool success = false;

            switch (accuracy) {
                case SequenceRemovalAccuracy::Exact: {
                    auto it = std::find(sequences_.begin(), sequences_.end(), sequence);
                    if (it != sequences_.end()) {
                        sequences_.erase(it);
                        success = true;
                    }
                    break;
                }
                case SequenceRemovalAccuracy::LowerBound: {
                    auto it = std::upper_bound(sequences_.begin(), sequences_.end(), sequence);
                    if (it != sequences_.begin()) {
                        sequences_.erase(sequences_.begin(), it);
                        success = true;
                    }
                    break;
                }
                case SequenceRemovalAccuracy::UpperBound: {
                    auto it = std::lower_bound(sequences_.begin(), sequences_.end(), sequence);
                    if (it != sequences_.end()) {
                        sequences_.erase(it, sequences_.end());
                        success = true;
                    }
                    break;
                }
            }
            return success;
        }
        inline void setSequences(const PoolsRequestedSequences& sequences) {
            resetSequences();
            sequences_ = sequences;
        }
        inline void addSequences(const cs::Sequence sequence) {
            sequences_.push_back(sequence);
        }
        inline void addSequences(cs::Sequence begin, cs::Sequence end) {
            for (;begin <= end; ++begin) {
                sequences_.push_back(begin);
            }
        }
        inline void addSequences(const PoolsRequestedSequences& sequences) {
            sequences_.insert(sequences_.end(), sequences.begin(), sequences.end());
        }
        inline void orderSequences() {
            std::sort(std::begin(sequences_), std::end(sequences_));
        }
        inline void reset() {
            resetSequences();
        }
        inline void resetSequences() {
            sequences_.clear();
        }
        inline void setPublicKey(const cs::PublicKey& publicKey) {
            key_ = publicKey;
        }
        inline void setMaxSequence(cs::Sequence sequence) {
            maxSequence_ = sequence;
        }

        inline const cs::PublicKey& publicKey() const {
            return key_;
        }
        inline const PoolsRequestedSequences& sequences() const {
            return sequences_;
        }
        inline cs::Sequence maxSequence() const {
            return maxSequence_;
        }

        bool operator<(const Neighbour& other) const {
            return maxSequence_ < other.maxSequence_;
        }

        bool operator>(const Neighbour& other) const {
            return !((*this) < other);
        }

        bool operator==(const Neighbour& other) const {
            return key_ == other.key_;
        }

        bool operator!=(const Neighbour& other) const {
            return !((*this) == other);
        }

        friend std::ostream& operator<<(std::ostream& os, const Neighbour& el) {
            os << "seqs:";

            if (el.sequences_.empty()) {
                os << " empty";
            }
            else {
                for (const auto seq : el.sequences_) {
                    os << " " << seq;
                }
            }

            return os;
        }

    private:
        cs::Sequence maxSequence_ = 0;
        cs::PublicKey key_;                  // neighbour public key
        PoolsRequestedSequences sequences_;  // requested sequence
    };

protected:
    std::vector<std::pair<cs::PublicKey, cs::Sequence>> neighbours() const;

private:
    BlockChain* blockChain_;

    // flag starting  syncronization
    std::atomic<bool> isSyncroStarted_ = false;
    std::atomic<bool> canRequestFreeBlocks = true;
    const cs::RoundNumber MaxRoundDescrepancy = 5;

    // [key] = sequence,
    // [value] =  packet counter
    // value: increase each new round
    std::map<cs::Sequence, cs::RoundNumber> requestedSequences_;
    std::vector<Neighbour> neighbours_;

    cs::Timer timer_;

    friend std::ostream& operator<<(std::ostream&, const PoolSynchronizer::CounterType);
    friend std::ostream& operator<<(std::ostream&, const PoolSynchronizer::SequenceRemovalAccuracy);
};

inline std::ostream& operator<<(std::ostream& os, const PoolSynchronizer::SequenceRemovalAccuracy type) {
    switch (type) {
        case PoolSynchronizer::SequenceRemovalAccuracy::Exact:
            os << "Exact";
            break;
        case PoolSynchronizer::SequenceRemovalAccuracy::LowerBound:
            os << "LowerBound";
            break;
        case PoolSynchronizer::SequenceRemovalAccuracy::UpperBound:
            os << "UpperBound";
            break;
    }

    return os;
}
}  // namespace cs
#endif  // POOLSYNCHRONIZER_HPP
