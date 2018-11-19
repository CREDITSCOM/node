#ifndef POOLSYNCHRONIZER_H
#define POOLSYNCHRONIZER_H

#include "blockchain.hpp"
#include <csnode/nodecore.hpp>
#include <csnode/packstream.hpp>
#include <csdb/pool.h>

#include <lib/system/signals.hpp>
#include <lib/system/timer.hpp>

#include <net/neighbourhood.hpp>

class Node;

namespace cs {

    using PoolSynchronizerRequestSignal = cs::Signal<void(const ConnectionPtr& target, const PoolsRequestedSequences sequences)>;
    using PoolSynchronizerSynchroFinished = cs::Signal<void()>;

    class PoolSynchronizer
    {
    public: // Interface

        explicit PoolSynchronizer(Transport* transport, BlockChain* blockChain);

        void processingSync(const cs::RoundNumber roundNum);

        // syncro get functions
        void getBlockReply(cs::PoolsBlock&& poolsBlock);

        // syncro send functions
        void sendBlockRequest();

        bool isSyncroStarted() const;

        // pool sync progress
        void showSyncronizationProgress(const csdb::Pool::sequence_t lastWrittenSequence);

    public signals: // Signals

        PoolSynchronizerRequestSignal sendRequest;
        PoolSynchronizerSynchroFinished synchroFinished;

    private: // Service

        bool checkActivity();

        void sendBlock(const ConnectionPtr& target, const PoolsRequestedSequences& sequences);

        void addToTemporaryStorage(const csdb::Pool& pool);
        csdb::Pool::sequence_t processingTemporaryStorage();

        bool getPoolRequestedSequences();

    private: // Members

        Transport* m_transport;
        BlockChain* m_blockChain;

        inline static const int m_maxBlockCount = 4;
        const int m_maxWaitingTimeReply;

        // syncro variables
        bool m_isSyncroStarted = false;

        // sync meta
        cs::RoundNumber m_roundToSync = 0;

        // to store new blocks
        std::map<csdb::Pool::sequence_t, csdb::Pool> m_temporaryStorage;

        // [key] = sequence,
        // [value] = m_maxWaitingTimeReply
        // value: Decreases, soon as a response is received for another requested block.
        std::map<csdb::Pool::sequence_t, int> m_requestedSequences;

        PoolsRequestedSequences m_receivedSequences;
    };


}

#endif // POOLSYNCHRONIZER_H
