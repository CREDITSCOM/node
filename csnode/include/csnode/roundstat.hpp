#ifndef ROUNDSTAT_HPP
#define ROUNDSTAT_HPP

#include <csdb/pool.hpp>

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

using PingCheckSignal = cs::Signal<void(cs::Sequence, const cs::PublicKey&)>;

struct NodeStat {
    bool nodeOn;
    std::string ip;
    std::string version;
    std::string platform;
    uint64_t timeReg;
    uint64_t timeActive;
    uint64_t trustedDay;
    uint64_t trustedMonth;
    uint64_t trustedPrevMonth;
    uint64_t trustedTotal;
    uint64_t failedTrustedDay;
    uint64_t failedTrustedMonth;
    uint64_t failedTrustedPrevMonth;
    uint64_t failedTrustedTotal;
    uint64_t trustedADay;
    uint64_t trustedAMonth;
    uint64_t trustedAPrevMonth;
    uint64_t trustedATotal;
    uint64_t failedTrustedADay;
    uint64_t failedTrustedAMonth;
    uint64_t failedTrustedAPrevMonth;
    uint64_t failedTrustedATotal;
    csdb::Amount feeDay;
    csdb::Amount feeMonth;
    csdb::Amount feePrevMonth;
    csdb::Amount feeTotal;
    cs::Sequence lastConsensus = 0ULL;
};

class RoundStat {
public:
    RoundStat();

    void onRoundStart(cs::RoundNumber round, bool skipLogs);

    // called when next block is read from database
    void onReadBlock(const csdb::Pool& block, bool* shouldStop);

    void onStopReadingFromDb(uint64_t totalTransactions);

    // called when next block is stored
    void onStoreBlock(const csdb::Pool& block);

    void setNodeStatus(const cs::PublicKey key, bool status);

    size_t totalTransactions() const {
        return totalAcceptedTransactions_;
    }

    size_t uptimeMs() const;
    size_t aveRoundMs() const;
    size_t nodeStartRound() const;

    // returns duration from last round in ms,
    // only if connected to transport ping signal
    size_t lastRoundMs() const;

    void resetLastRoundMs();
    bool isCurrentRoundTooLong(size_t longDurationMs = kMaxRoundDelay) const;

    const std::map<cs::PublicKey, cs::NodeStat> getNodes() const {
        return  nodes_;
    }

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

private:
    void checkRoundElapse();
    void checkStoreBlockElapse();
    void countTrustAndTrx(const csdb::Pool& block);

    void dayChangeProcedure();
    void monthChangeProcedure();

    static const int64_t kMaxPingSynchroDelay = 30000;

    // amount of transactions received (to verify or not or to ignore)
    size_t totalReceivedTransactions_;

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
    std::map<cs::PublicKey, cs::NodeStat> nodes_;
    int lastMonth_ = 0;
    int lastDay_ = 0;
};

}  // namespace cs

#endif // ROUNDSTAT_HPP
