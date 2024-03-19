#ifndef ROUNDSTAT_HPP
#define ROUNDSTAT_HPP

#include <csdb/pool.hpp>
#include <csnode/blockchain.hpp> 

#include <lib/system/common.hpp>
#include <lib/system/signals.hpp>

#include <set>
#include <atomic>
#include <chrono>
#include <mutex>
#include <map>

class Config;

namespace cs {
constexpr size_t kMaxRoundDelay = 30000;
constexpr uint64_t daySeconds = 86400ULL;

using PingCheckSignal = cs::Signal<void(cs::Sequence, const cs::PublicKey&)>;
using AccountStartTimeSignal = cs::Signal<void(uint64_t&, const cs::PublicKey)>;

struct NodeStat {
    std::string toString();
    Bytes to_bytes();
    static NodeStat from_bytes(Bytes& data);

    bool nodeOn;
    std::string ip = "";
    std::string version ="";
    std::string platform ="";
    uint64_t timeReg = 0ULL;
    uint64_t timeFirstConsensus = 0ULL;
    uint64_t timeActive = 0ULL;
    uint64_t trustedDay = 0ULL;
    uint64_t trustedMonth = 0ULL;
    uint64_t trustedPrevMonth = 0ULL;
    uint64_t trustedTotal = 0ULL;
    uint64_t failedTrustedDay = 0ULL;
    uint64_t failedTrustedMonth = 0ULL;
    uint64_t failedTrustedPrevMonth = 0ULL;
    uint64_t failedTrustedTotal = 0ULL;
    uint64_t trustedADay = 0ULL;
    uint64_t trustedAMonth = 0ULL;
    uint64_t trustedAPrevMonth = 0ULL;
    uint64_t trustedATotal = 0ULL;
    uint64_t failedTrustedADay = 0ULL;
    uint64_t failedTrustedAMonth = 0ULL;
    uint64_t failedTrustedAPrevMonth = 0ULL;
    uint64_t failedTrustedATotal = 0ULL;
    csdb::Amount feeDay{ 0 };
    csdb::Amount feeMonth{ 0 };
    csdb::Amount feePrevMonth{ 0 };
    csdb::Amount feeTotal{ 0 };
    csdb::Amount rewardDay{ 0 };
    csdb::Amount rewardMonth{ 0 };
    csdb::Amount rewardPrevMonth{ 0 };
    csdb::Amount rewardTotal{ 0 };
    cs::Sequence lastConsensus = 0ULL;
};


struct MinedEvaluation{
    Bytes to_bytes();
    static MinedEvaluation from_bytes(Bytes& data);

    csdb::Amount rewardDay{ 0 };
    csdb::Amount rewardMonth{ 0 };
    csdb::Amount rewardPrevMonth{ 0 };
    csdb::Amount rewardTotal{ 0 };

};

struct MinedEvaluationDelegator {
    Bytes to_bytes();
    static MinedEvaluationDelegator from_bytes(Bytes& data);

    std::map<cs::PublicKey, cs::MinedEvaluation> me;
};



class RoundStat {
public:
    RoundStat(BlockChain* bch);

    void onRoundStart(cs::RoundNumber round, bool skipLogs);

    // called when next block is read from database
    void onReadBlock(const csdb::Pool& block, bool* shouldStop);

    void onStopReadingFromDb(uint64_t totalTransactions, bool successfulQuickStart);

    // called when next block is stored
    void onStoreBlock(const csdb::Pool& block);

    void setNodeStatus(const cs::PublicKey key, bool status);

    size_t totalTransactions() const {
        return totalAcceptedTransactions_;
    }

    size_t uptimeMs() const;
    size_t aveRoundMs() const;
    size_t nodeStartRound() const;

    MinedEvaluation getMined() {
        return totalMined_;
    }

    // returns duration from last round in ms,
    // only if connected to transport ping signal
    size_t lastRoundMs() const;

    void resetLastRoundMs();
    bool isCurrentRoundTooLong(size_t longDurationMs = kMaxRoundDelay) const;
    const std::map<cs::PublicKey, cs::MinedEvaluationDelegator>& getRoundEvaluation() {
        return minedEvaluation_;
    }

    const std::map<cs::PublicKey, cs::NodeStat> getNodes() const {
        return  nodes_;
    }

    //serialization try:
    void clear();
    void printClassInfo();
    Bytes serialize();
    void deserialize(Bytes& data);

public slots:
    void onPingReceived(cs::Sequence, const cs::PublicKey&);
    void checkPing(cs::Sequence sequence, const cs::PublicKey& key);

    void onRoundChanged();
    void onBlockStored();
    void onMainThreadIterated();

public signals:
    cs::Action roundTimeElapsed;
    cs::Action storeBlockTimeElapsed;
    PingCheckSignal pingChecked;
    AccountStartTimeSignal accountInitiationRequest;

private:
    void checkRoundElapse();
    void checkStoreBlockElapse();
    void countTrustAndTrx(const csdb::Pool& block);
    void fillMinedEvaluation(const cs::PublicKeys& confidants, const std::vector<csdb::Amount>& rew);
    void dayChangeProcedure(uint64_t cTime);
    void monthChangeProcedure();

    static const int64_t kMaxPingSynchroDelay = 30000;
    //<to save>
    // amount of transactions received (to verify or not or to ignore)
    size_t totalReceivedTransactions_;
    size_t totalBlockChainTransactions_ = 0;

    //<to save>
    // amount of accepted transactions (stored in blockchain)
    std::atomic<size_t> totalAcceptedTransactions_;

    // amount of deferred transactions (in deferred block)
    size_t deferredTransactionsCount_;
    std::chrono::steady_clock::time_point startPointMs_;

    size_t totalDurationMs_;
    size_t aveRoundMs_;

    size_t nodeStartRound_;
    size_t startSkipRounds_;

    std::atomic<size_t> lastRoundMs_;

    // round time elapsing calcualtion, store block elapsing and sync
    std::mutex statsElapseMutex_;
    std::chrono::steady_clock::time_point roundElapseTimePoint_;
    std::chrono::steady_clock::time_point storeBlockElapseTimePoint_;

    std::chrono::milliseconds checkPingDelta_{0};
    //<to save>
    std::map<cs::PublicKey, cs::NodeStat> nodes_;
    //<to save>
    std::map<cs::PublicKey, cs::MinedEvaluationDelegator> minedEvaluation_; //node key (minedEvaluation(DelegatorKey, reward))
    int lastMonth_ = 0;
    int lastDay_ = 0;
    BlockChain* blockChain_;
    //<to save>
    MinedEvaluation totalMined_;
    size_t currentSessionBlockCounter_ = 0ULL;
    friend class RoundStat_Serializer;
};

}  // namespace cs

#endif // ROUNDSTAT_HPP
