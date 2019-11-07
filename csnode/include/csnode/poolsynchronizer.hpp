#ifndef POOLSYNCHRONIZER_HPP
#define POOLSYNCHRONIZER_HPP

#include <csdb/pool.hpp>

#include <csnode/blockchain.hpp>
#include <csnode/nodecore.hpp>
#include <csnode/packstream.hpp>

#include <lib/system/timer.hpp>
#include <lib/system/signals.hpp>
#include <lib/system/lockfreechanger.hpp>

#include <net/neighbourhood.hpp>

#include <config.hpp>

class Node;
class Transport;

namespace cs {
using PoolSynchronizerRequestSignal = cs::Signal<void(const PublicKey& target, const PoolsRequestedSequences& sequences, std::size_t packet)>;

class PoolSynchronizer {
public:
    explicit PoolSynchronizer(const PoolSyncData& data, Transport* transport, BlockChain* blockChain);

    void sync(cs::RoundNumber roundNum, cs::RoundNumber difference = roundDifferentForSync, bool isBigBand = false);
    void syncLastPool();

    // syncro get functions
    void getBlockReply(cs::PoolsBlock&& poolsBlock, std::size_t packetNum);

    // syncro send functions
    void sendBlockRequest();

    bool isSyncroStarted() const;

    bool isOneBlockReply() const;

    bool isFastMode() const;

    static const cs::RoundNumber roundDifferentForSync = cs::values::kDefaultMetaStorageMaxSize;

public signals:
    PoolSynchronizerRequestSignal sendRequest;

public slots:
    void onConfigChanged(const Config& updated);
};
}  // namespace cs
#endif  // POOLSYNCHRONIZER_HPP
