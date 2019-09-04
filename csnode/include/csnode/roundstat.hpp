#pragma once

#include <csdb/pool.hpp>
#include <lib/system/common.hpp>

#include <chrono>
#include <set>

namespace cs {
constexpr size_t MaxStoredDurations = 1000;

class RoundStat {
public:
    RoundStat();

    void onRoundStart(cs::RoundNumber round, bool skip_logs);

    // called when next block is read from database
    void onReadBlock(csdb::Pool block, bool* should_stop);

    // called when next block is stored
    void onStoreBlock(csdb::Pool block);

    size_t total_transactions() const {
        return totalAcceptedTransactions_;
    }

    size_t getAveTime();

	size_t getNodeStartRound();

private:
    // amount of transactions received (to verify or not or to ignore)
    size_t totalReceivedTransactions_;

    // amount of accepted transactions (stored in blockchain)
    size_t totalAcceptedTransactions_;

    // amount of deferred transactions (in deferred block)
    size_t deferredTransactionsCount_;
    std::chrono::steady_clock::time_point startPointMs_;
    size_t totalDurationMs_;
    size_t ave_round_ms;

    // std::multiset<size_t> shortestRounds_;
    // std::multiset<size_t> longestRounds_;

    size_t node_start_round;
    size_t start_skip_rounds;
};

}  // namespace cs
