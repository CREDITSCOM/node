#ifndef POOLSYNCHRONIZER_HPP
#define POOLSYNCHRONIZER_HPP

#include <csdb/pool.hpp>
#include <csnode/blockchain.hpp>
#include <csnode/nodecore.hpp>
#include <csnode/packstream.hpp>

#include <lib/system/signals.hpp>
#include <lib/system/timer.hpp>

#include <net/neighbourhood.hpp>

#include <client/config.hpp>

class Node;

namespace cs {

using PoolSynchronizerRequestSignal =
  cs::Signal<void(const ConnectionPtr target, const PoolsRequestedSequences& sequences, uint32_t packet)>;

class PoolSynchronizer {
public:  // Interface
  explicit PoolSynchronizer(const PoolSyncData& data, Transport* transport, BlockChain* blockChain);

  void processingSync(cs::RoundNumber roundNum, bool isBigBand = false);

  // syncro get functions
  void getBlockReply(cs::PoolsBlock&& poolsBlock, uint32_t packet);

  // syncro send functions
  void sendBlockRequest();

  bool isSyncroStarted() const;

public signals:  // Signals

  PoolSynchronizerRequestSignal sendRequest;

private slots:
  void onTimeOut();

private:  // Service
  enum class CounterType;
  class NeighboursSetElemet;

  // pool sync progress
  bool showSyncronizationProgress(const csdb::Pool::sequence_t lastWrittenSequence) const;

  bool checkActivity(const CounterType& counterType);

  void sendBlock(const NeighboursSetElemet& neighbour);

  bool getNeededSequences(NeighboursSetElemet& neighbour);

  void checkNeighbourSequence(const csdb::Pool::sequence_t sequence);
  void refreshNeighbours();

  bool isLastRequest() const;

  bool isAvailableRequest(const cs::PoolSynchronizer::NeighboursSetElemet& nh) const;

  void synchroFinished();

  void printNeighbours(const std::string& funcName) const;

private:  // struct
  enum class CounterType
  {
    ROUND,
    TIMER
  };

  class NeighboursSetElemet {
  public:
    explicit NeighboursSetElemet(uint8_t neighbourIndex, uint8_t blockPoolsCount)
    : m_neighbourIndex(neighbourIndex)
    , m_roundCounter(0) {
      m_sequences.reserve(blockPoolsCount);
    }

    inline void removeSequnce(const csdb::Pool::sequence_t sequence) {
      const auto it = std::find(m_sequences.begin(), m_sequences.end(), sequence);
      if (it != m_sequences.end()) {
        m_sequences.erase(it);
      }
    }
    inline void setSequences(const PoolsRequestedSequences& sequences) {
      resetSequences();
      m_sequences = sequences;
    }
    inline void addSequences(const RoundNumber sequence) {
      m_sequences.push_back(sequence);
    }
    inline void reset() {
      resetSequences();
      resetRoundCounter();
    }
    inline void resetSequences() {
      m_sequences.clear();
    }
    inline void resetRoundCounter() {
      m_roundCounter = 0;
    }
    inline void setIndex(const uint8_t num) {
      m_neighbourIndex = num;
    }

    inline uint8_t index() const {
      return m_neighbourIndex;
    }
    inline const PoolsRequestedSequences& sequences() const {
      return m_sequences;
    }
    inline uint32_t roundCounter() const {
      return m_roundCounter;
    }

    inline void increaseRoundCounter() {
      if (!m_sequences.empty()) {
        ++m_roundCounter;
      }
    }

    bool operator<(const NeighboursSetElemet& other) const {
      if (m_sequences.empty() || other.m_sequences.empty()) {
        return m_sequences.size() > other.m_sequences.size();
      }

      return m_sequences.front() < other.m_sequences.front();
    }

    friend std::ostream& operator<<(std::ostream& os, const NeighboursSetElemet& el) {
      os << "idx: " << cs::numeric_cast<int>(el.m_neighbourIndex) << ", seqs:";

      if (el.m_sequences.empty()) {
        os << " empty";
      }
      else {
        for (const auto seq : el.m_sequences) {
          os << " " << seq;
        }
      }

      os << ", round counter: " << el.m_roundCounter;

      return os;
    }

  private:
    uint8_t m_neighbourIndex;             // neighbour number
    PoolsRequestedSequences m_sequences;  // requested sequence
    uint32_t m_roundCounter;
  };

private:  // Members
  inline static const cs::RoundNumber s_roundDifferentForSync = cs::values::defaultMetaStorageMaxSize;

  const PoolSyncData m_syncData;

  Transport* m_transport;
  BlockChain* m_blockChain;

  // flag starting  syncronization
  bool m_isSyncroStarted = false;
  // [key] = sequence,
  // [value] =  packet counter
  // value: increase each new round
  std::map<csdb::Pool::sequence_t, uint32_t> m_requestedSequences;

  std::vector<NeighboursSetElemet> m_neighbours;

  cs::Timer m_timer;

  friend std::ostream& operator<<(std::ostream&, const PoolSynchronizer::CounterType&);
};

inline std::ostream& operator<<(std::ostream& os, const PoolSynchronizer::CounterType& type) {
  switch (type) {
  case PoolSynchronizer::CounterType::ROUND:
    os << "ROUND";
    break;
  case PoolSynchronizer::CounterType::TIMER:
    os << "TIMER";
    break;
  default:
    break;
  }

  return os;
}
}  // namespace cs

#endif  // POOLSYNCHRONIZER_HPP
