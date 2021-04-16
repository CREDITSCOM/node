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
constexpr size_t kMaxRoundDelay = 30000;

using PingCheckSignal = cs::Signal<void(cs::Sequence, const cs::PublicKey&)>;

class RoundStat {
public:
    RoundStat();

    void onRoundStart(cs::RoundNumber round, bool skipLogs);

    // called when next block is read from database
    void onReadBlock(const csdb::Pool& block, bool* shouldStop);

    // called when next block is stored
    void onStoreBlock(const csdb::Pool& block);

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
};

}  // namespace cs

#endif // ROUNDSTAT_HPP
