#ifndef ROUNDSTAT_HPP
#define ROUNDSTAT_HPP

#include <csdb/pool.hpp>

#include <lib/system/common.hpp>
#include <lib/system/signals.hpp>

#include <set>
#include <atomic>
#include <chrono>
#include <mutex>

class Config;

namespace cs {
constexpr size_t kMaxStoredDurations = 1000;
constexpr size_t kMaxRoundDelay = 30000;

class RoundStat {
public:
    RoundStat();

    void onRoundStart(cs::RoundNumber round, bool skipLogs);

    // called when next block is read from database
    void onReadBlock(csdb::Pool block, bool* shouldStop);

    // called when next block is stored
    void onStoreBlock(csdb::Pool block);

    size_t totalTransactions() const {
        return totalAcceptedTransactions_;
    }

    size_t aveTime();
    size_t nodeStartRound();

    // returns duration from last round in ms,
    // only if connected to transport ping signal
    size_t lastRoundMs() const;

    void resetLastRoundMs();
    bool isLastRoundTooLong() const;

public slots:
    void onPingReceived(cs::Sequence, const cs::PublicKey&);
    void onRoundChanged();
    void onMainThreadIterated();

public signals:
    cs::Action roundTimeElapsed;

private:
    // amount of transactions received (to verify or not or to ignore)
    size_t totalReceivedTransactions_;

    // amount of accepted transactions (stored in blockchain)
    size_t totalAcceptedTransactions_;

    // amount of deferred transactions (in deferred block)
    size_t deferredTransactionsCount_;
    std::chrono::steady_clock::time_point startPointMs_;

    size_t totalDurationMs_;
    size_t aveRoundMs_;

    size_t nodeStartRound_;
    size_t startSkipRounds_;

    std::atomic<size_t> lastRoundMs_;

    // round time elapsing calcualtion and sync
    std::mutex roundElapseMutex_;
    uint64_t roundElapseSetting_;
    std::chrono::steady_clock::time_point roundElapseTimePoint_;
};

}  // namespace cs

#endif // ROUNDSTAT_HPP
