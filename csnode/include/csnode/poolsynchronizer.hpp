#ifndef POOLSYNCHRONIZER_HPP
#define POOLSYNCHRONIZER_HPP

#include <csdb/pool.hpp>

#include <csnode/blockchain.hpp>
#include <csnode/nodecore.hpp>

#include <lib/system/common.hpp>
#include <lib/system/timer.hpp>
#include <lib/system/signals.hpp>

#include <net/neighbourhood.hpp>

namespace cs {
using PoolSynchronizerRequestSignal = Signal<void(const PublicKey& target, const PoolsRequestedSequences& sequences)>;

class PoolSynchronizer {
public:
    explicit PoolSynchronizer(BlockChain* blockChain);

    void sync(RoundNumber roundNum, RoundNumber difference = kRoundDifferentForSync);
    void syncLastPool();
    void getBlockReply(PoolsBlock&& poolsBlock);
    bool isSyncroStarted() const;

    static const RoundNumber kRoundDifferentForSync = values::kDefaultMetaStorageMaxSize;

public signals:
    PoolSynchronizerRequestSignal sendRequest;

public slots:
    void onStoreBlockTimeElapsed();
    void onPingReceived(Sequence sequence, const PublicKey& publicKey);
    void onNeighbourAdded(const PublicKey&, Sequence) {}
    void onNeighbourRemoved(const PublicKey& publicKey);

private slots:
    void onTimeOut();

private:
    void sendBlockRequest();
    void sendBlock(const PublicKey& neighbour, const PoolsRequestedSequences& sequences);

    bool showSyncronizationProgress(Sequence lastWrittenSequence) const;

    std::vector<Sequence> getNeededSequences(
        const std::vector<BlockChain::SequenceInterval>& requiredBlocks,
        Sequence neighbourLastSeq
    );

    void synchroFinished();
    size_t nextIndex(size_t index) const;

    BlockChain* blockChain_;

    std::atomic<bool> isSyncroStarted_ = false;

    Sequence maxRequestedSequence_ = kWrongSequence;
    std::unordered_map<PublicKey, Sequence> neighbours_;

    Timer timer_;
};
}  // namespace cs
#endif  // POOLSYNCHRONIZER_HPP
