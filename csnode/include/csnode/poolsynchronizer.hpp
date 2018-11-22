#ifndef POOLSYNCHRONIZER_HPP
#define POOLSYNCHRONIZER_HPP

#include <csdb/pool.h>
#include <csnode/nodecore.hpp>
#include <csnode/packstream.hpp>
#include <csnode/blockchain.hpp>

#include <lib/system/signals.hpp>
#include <lib/system/timer.hpp>

#include <net/neighbourhood.hpp>

class Node;

namespace cs {

using PoolSynchronizerRequestSignal =
    cs::Signal<void(const ConnectionPtr& target, const PoolsRequestedSequences sequences, uint32_t packCounter)>;
using PoolSynchronizerSynchroFinished = cs::Signal<void()>;

class PoolSynchronizer {
public:  // Interface
  explicit PoolSynchronizer(Transport* transport, BlockChain* blockChain);

  void processingSync(const cs::RoundNumber roundNum);

  // syncro get functions
  void getBlockReply(cs::PoolsBlock&& poolsBlock, uint32_t packCounter);

  // syncro send functions
  void sendBlockRequest();

  bool isSyncroStarted() const;

public
signals:  // Signals

  PoolSynchronizerRequestSignal sendRequest;
  PoolSynchronizerSynchroFinished synchroFinished;

private:  // Service
  // pool sync progress
  void showSyncronizationProgress(const csdb::Pool::sequence_t lastWrittenSequence);

  bool checkActivity(bool isRound = true);

  void sendBlock(const ConnectionPtr& target, const PoolsRequestedSequences& sequences);

  void addToTemporaryStorage(const csdb::Pool& pool);
  csdb::Pool::sequence_t processingTemporaryStorage();

  bool getNeededSequences();

  void checkNeighbourSequence(const csdb::Pool::sequence_t sequence);
  void refreshNeighbours();

private:  // Members
  Transport* m_transport;
  BlockChain* m_blockChain;

  inline static const int m_maxBlockCount = 4;
  inline static const cs::RoundNumber s_roundDifferent = 2;
  inline static const int m_maxWaitingTimeReply = 15;  // reply count
  inline static const int m_maxWaitingTimeRound = 4;  // round count

  // syncro variables
  bool m_isSyncroStarted = false;

  // sync meta
  cs::RoundNumber m_roundToSync = 0;

  // to store new blocks
  std::map<csdb::Pool::sequence_t, csdb::Pool> m_temporaryStorage;

  struct WaitinTimeReply {
    explicit WaitinTimeReply(int round, int replyCount)
    : roundCount(round)
    , replyBlockCount(replyCount)
    , packCounter(0) {
    }

    int roundCount = 0;
    int replyBlockCount = 0;
    uint32_t packCounter = 0;
  };
  // [key] = sequence,
  // [value] = m_maxWaitingTimeReply
  // value: Decreases, soon as a response is received for another requested block or init new round.
  std::map<csdb::Pool::sequence_t, WaitinTimeReply> m_requestedSequences;

  PoolsRequestedSequences m_neededSequences;

  struct NeighboursSetElemet {
    NeighboursSetElemet(csdb::Pool::sequence_t seq, ConnectionPtr conn = ConnectionPtr())
    : sequence(seq)
    , connection(conn) {
    }

    csdb::Pool::sequence_t sequence = 0;  // requested sequence
    ConnectionPtr connection;             // neighbour

    const bool operator<(const NeighboursSetElemet& rhs) const {
      return sequence < rhs.sequence;
    }
  };

  std::vector<NeighboursSetElemet> m_neighbours;
};

}  // namespace cs

#endif  // POOLSYNCHRONIZER_HPP
