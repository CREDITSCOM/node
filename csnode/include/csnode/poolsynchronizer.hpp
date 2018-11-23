#ifndef POOLSYNCHRONIZER_HPP
#define POOLSYNCHRONIZER_HPP

#include <csdb/pool.h>
#include <csnode/blockchain.hpp>
#include <csnode/nodecore.hpp>
#include <csnode/packstream.hpp>

#include <lib/system/signals.hpp>
#include <lib/system/timer.hpp>

#include <net/neighbourhood.hpp>

class Node;

namespace cs {

using PoolSynchronizerRequestSignal =
    cs::Signal<void(const ConnectionPtr& target, const PoolsRequestedSequences sequences, uint32_t packet)>;
using PoolSynchronizerSynchroFinished = cs::Signal<void()>;

class PoolSynchronizer {
public:  // Interface
  explicit PoolSynchronizer(Transport* transport, BlockChain* blockChain);

  void processingSync(const cs::RoundNumber roundNum);

  // syncro get functions
  void getBlockReply(cs::PoolsBlock&& poolsBlock, uint32_t packet);

  // syncro send functions
  void sendBlockRequest();

  bool isSyncroStarted() const;

public signals:  // Signals

  PoolSynchronizerRequestSignal sendRequest;
  PoolSynchronizerSynchroFinished synchroFinished;

private:  // Service
  // pool sync progress
  void showSyncronizationProgress(const csdb::Pool::sequence_t lastWrittenSequence);

  bool checkActivity();

  void sendBlock(const ConnectionPtr& target);

  void addToTemporaryStorage(const csdb::Pool& pool);
  csdb::Pool::sequence_t processingTemporaryStorage();

  bool getNeededSequences();

  void checkNeighbourSequence(const csdb::Pool::sequence_t sequence);
  void refreshNeighbours();

  bool isLastRequest();

private:  // Members
  inline static const uint8_t s_maxBlockCount = 4;
  inline static const cs::RoundNumber s_roundDifferentForSync = 2;
  inline static const uint8_t s_roundDifferentForRepeatRequest = 4;  // round count for repeat request : 0 - every round
  inline static const uint8_t s_packetCountForHelp = 3;              // packet Counter for connect another neighbor

  Transport* m_transport;
  BlockChain* m_blockChain;

  // flag starting  syncronization
  bool m_isSyncroStarted = false;
  // number of the last needed sequence
  cs::RoundNumber m_roundToSync = 0;
  // array needed sequences for send request
  PoolsRequestedSequences m_neededSequences;
  // [key] = sequence,
  // [value] =  packet counter
  // value: increase each new round
  std::map<csdb::Pool::sequence_t, uint32_t> m_requestedSequences;
  // [key] = sequence,
  // [value] =  pool
  // to store new blocks
  std::map<csdb::Pool::sequence_t, csdb::Pool> m_temporaryStorage;

  struct NeighboursSetElemet {
    explicit NeighboursSetElemet(ConnectionPtr conn, csdb::Pool::sequence_t sequence = 0)
    : m_Connection(conn)
    , m_Sequence(sequence)
    , m_RoundCounter(0) {
    }

    inline bool isEqual(csdb::Pool::sequence_t sequence) const {
      return m_Sequence == sequence;
    }
    inline bool isAvailableSequence() const {
      return isEqual(0);
    }
    inline void setSequence(csdb::Pool::sequence_t sequence) {
      m_Sequence = sequence;
    }
    inline void reset() {
      m_Sequence = 0;
      m_RoundCounter = 0;
    }

    inline const ConnectionPtr& connection() const {
      return m_Connection;
    }
    inline csdb::Pool::sequence_t sequence() const {
      return m_Sequence;
    }
    inline uint32_t roundCounter() const {
      return m_RoundCounter;
    }

    inline bool isAvailableRequest() const {
      return m_RoundCounter > s_roundDifferentForRepeatRequest;
    }
    inline bool increaseRoundCounter() {
      ++m_RoundCounter;
      return isAvailableRequest();
    }

  private:
    ConnectionPtr m_Connection;         // neighbour
    csdb::Pool::sequence_t m_Sequence;  // requested sequence
    uint32_t m_RoundCounter;
  };

  std::vector<NeighboursSetElemet> m_neighbours;
};

}  // namespace cs

#endif  // POOLSYNCHRONIZER_HPP
