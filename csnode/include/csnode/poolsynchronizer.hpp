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

private:  // Service
  // pool sync progress
  void showSyncronizationProgress(const csdb::Pool::sequence_t lastWrittenSequence);

  bool checkActivity();

  void sendBlock(uint8_t neighbourNum);

  bool getNeededSequences(uint8_t nieghbourNumber);

  void checkNeighbourSequence(const csdb::Pool::sequence_t sequence);
  void refreshNeighbours();

  bool isLastRequest();

  void synchroFinished();

private:  // Members
  inline static const uint8_t s_maxBlockCount = 6;
  inline static const cs::RoundNumber s_roundDifferentForSync = cs::values::defaultMetaStorageMaxSize;
  inline static const uint8_t s_roundDifferentForRepeatRequest = 0;  // round count for repeat request : 0 - every round
  inline static const uint8_t s_packetCountForHelp = 10;             // packet Counter for connect another neighbor

  Transport* m_transport;
  BlockChain* m_blockChain;

  // flag starting  syncronization
  bool m_isSyncroStarted = false;
  // array needed sequences for send request
  PoolsRequestedSequences m_neededSequences;
  // [key] = sequence,
  // [value] =  packet counter
  // value: increase each new round
  std::map<csdb::Pool::sequence_t, uint32_t> m_requestedSequences;

  struct NeighboursSetElemet {
    explicit NeighboursSetElemet(uint8_t neighbourNum, csdb::Pool::sequence_t sequence = 0)
    : m_neighbourNum(neighbourNum)
    , m_sequence(sequence)
    , m_roundCounter(0) {
    }

    inline bool isEqual(csdb::Pool::sequence_t sequence) const {
      return m_sequence == sequence;
    }
    inline bool isAvailableSequence() const {
      return isEqual(0);
    }
    inline void setSequence(csdb::Pool::sequence_t sequence) {
      m_sequence = sequence;
    }
    inline void reset() {
      m_sequence = 0;
      m_roundCounter = 0;
    }
    inline void setNeighbourNum(uint8_t num) {
      m_neighbourNum = num;
    }

    inline uint8_t neighbourNum() const {
      return m_neighbourNum;
    }
    inline csdb::Pool::sequence_t sequence() const {
      return m_sequence;
    }
    inline uint32_t roundCounter() const {
      return m_roundCounter;
    }

    inline bool isAvailableRequest() const {
      return m_roundCounter > s_roundDifferentForRepeatRequest;
    }
    inline bool increaseRoundCounter() {
      ++m_roundCounter;
      return isAvailableRequest();
    }

  private:
    uint8_t m_neighbourNum;             // neighbour number
    csdb::Pool::sequence_t m_sequence;  // requested sequence
    uint32_t m_roundCounter;
  };

  std::vector<NeighboursSetElemet> m_neighbours;
};

}  // namespace cs

#endif  // POOLSYNCHRONIZER_HPP
