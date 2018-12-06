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
    cs::Signal<void(const ConnectionPtr& target, const PoolsRequestedSequences sequences, uint32_t packet)>;

class PoolSynchronizer {
public:  // Interface
  explicit PoolSynchronizer(const PoolSyncData& data, Transport* transport, BlockChain* blockChain);

  void processingSync(const cs::RoundNumber roundNum, bool isBigBand = false);

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
  void showSyncronizationProgress(const csdb::Pool::sequence_t lastWrittenSequence);

  bool checkActivity(const CounterType& counterType);

  void sendBlock(uint8_t neighbourNum);

  bool getNeededSequences(uint8_t nieghbourNumber);

  void checkNeighbourSequence(const csdb::Pool::sequence_t sequence);
  void refreshNeighbours();

  bool isLastRequest();

  bool isAvailableRequest(const cs::PoolSynchronizer::NeighboursSetElemet& nh) const;

  void synchroFinished();

  void printNeighbours(const std::string& funcName);

private:  // struct
  enum class CounterType
  {
    ROUND,
    TIMER
  };

  class NeighboursSetElemet {
  public:
    explicit NeighboursSetElemet(uint8_t neighbourNum, uint8_t blockPoolsCount)
    : m_neighbourNum(neighbourNum)
    , m_roundCounter(0) {
      m_sequences.reserve(blockPoolsCount);
    }

    inline void removeSequnce(csdb::Pool::sequence_t sequence) {
      const auto it = std::find(m_sequences.begin(), m_sequences.end(), sequence);
      if (it != m_sequences.end()) {
        m_sequences.erase(it);
      }
    }
    inline bool isAvailableSequences() const {
      return m_sequences.empty();
    }
    inline void setSequences(const PoolsRequestedSequences& sequences) {
      m_sequences.clear();
      m_sequences = sequences;
    }
    inline void reset() {
      m_sequences.clear();
      resetRoundCounter();
    }
    inline void resetRoundCounter() {
      m_roundCounter = 0;
    }
    inline void setNeighbourNum(uint8_t num) {
      m_neighbourNum = num;
    }

    inline uint8_t neighbourNum() const {
      return m_neighbourNum;
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
      os << "num: " << cs::numeric_cast<int>(el.m_neighbourNum) << ", seqs: ";

      if (el.m_sequences.empty()) {
        os << "empty";
      }
      else {
        for (const auto seq : el.m_sequences) {
          os << seq << " ";
        }
      }

      os << ", round counter: " << el.m_roundCounter;

      return os;
    }

  private:
    uint8_t m_neighbourNum;               // neighbour number
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
  // array needed sequences for send request
  PoolsRequestedSequences m_neededSequences;
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
