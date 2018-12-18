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
    cs::Signal<void(const ConnectionPtr target, const PoolsRequestedSequences& sequences, std::size_t packet)>;

class PoolSynchronizer {
public:  // Interface
  explicit PoolSynchronizer(const PoolSyncData& data, Transport* transport, BlockChain* blockChain);

  void processingSync(cs::RoundNumber roundNum, bool isBigBand = false);

  // syncro get functions
  void getBlockReply(cs::PoolsBlock&& poolsBlock, std::size_t packetNum);

  // syncro send functions
  void sendBlockRequest();

  bool isSyncroStarted() const;

  bool isOneBlockReply() const;

public signals:  // Signals

  PoolSynchronizerRequestSignal sendRequest;

private slots:
  void onTimeOut();

  void onWriteBlock(const csdb::Pool::sequence_t sequence);

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
    : neighbourIndex_(neighbourIndex)
    , roundCounter_(0) {
      sequences_.reserve(blockPoolsCount);
    }

    inline void removeSequnce(const csdb::Pool::sequence_t sequence) {
      if (sequences_.empty()) {
        return;
      }

      const auto it = std::find(sequences_.begin(), sequences_.end(), sequence);
      if (it != sequences_.end()) {
        sequences_.erase(it);
      }
    }
    inline void setSequences(const PoolsRequestedSequences& sequences) {
      resetSequences();
      sequences_ = sequences;
    }
    inline void addSequences(const csdb::Pool::sequence_t sequence) {
      sequences_.push_back(sequence);
    }
    inline void reset() {
      resetSequences();
      resetRoundCounter();
    }
    inline void resetSequences() {
      sequences_.clear();
    }
    inline void resetRoundCounter() {
      roundCounter_ = 0;
    }
    inline void setIndex(const uint8_t num) {
      neighbourIndex_ = num;
    }

    inline uint8_t index() const {
      return neighbourIndex_;
    }
    inline const PoolsRequestedSequences& sequences() const {
      return sequences_;
    }
    inline cs::RoundNumber roundCounter() const {
      return roundCounter_;
    }

    inline void increaseRoundCounter() {
      if (!sequences_.empty()) {
        ++roundCounter_;
      }
    }

    bool operator<(const NeighboursSetElemet& other) const {
      if (sequences_.empty() || other.sequences_.empty()) {
        return sequences_.size() > other.sequences_.size();
      }

      return sequences_.front() < other.sequences_.front();
    }

    friend std::ostream& operator<<(std::ostream& os, const NeighboursSetElemet& el) {
      os << "idx: " << cs::numeric_cast<int>(el.neighbourIndex_) << ", seqs:";

      if (el.sequences_.empty()) {
        os << " empty";
      }
      else {
        for (const auto seq : el.sequences_) {
          os << " " << seq;
        }
      }

      os << ", round counter: " << el.roundCounter_;

      return os;
    }

  private:
    uint8_t neighbourIndex_;             // neighbour number
    PoolsRequestedSequences sequences_;  // requested sequence
    cs::RoundNumber roundCounter_;
  };

private:  // Members
  inline static const cs::RoundNumber roundDifferentForSync_ = cs::values::defaultMetaStorageMaxSize;

  const PoolSyncData syncData_;

  Transport* transport_;
  BlockChain* blockChain_;

  // flag starting  syncronization
  bool isSyncroStarted_ = false;
  // [key] = sequence,
  // [value] =  packet counter
  // value: increase each new round
  std::map<csdb::Pool::sequence_t, cs::RoundNumber> requestedSequences_;

  std::vector<NeighboursSetElemet> neighbours_;

  cs::Timer timer_;

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
  }

  return os;
}
}  // namespace cs

#endif  // POOLSYNCHRONIZER_HPP
