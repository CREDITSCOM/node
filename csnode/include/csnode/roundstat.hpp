#pragma once

#include <chrono>
#include <lib/system/common.hpp>
#include <set>

namespace cs {
constexpr size_t MaxStoredDurations = 1000;

class RoundStat {
public:
  RoundStat();

  void onRoundStart(cs::RoundNumber round);

  // amount of transactions received (to verify or not or to ignore)
  size_t totalReceivedTransactions_;

  // amount of accepted transactions (stored in blockchain)
  size_t totalAcceptedTransactions_;

  // amount of deferred transactions (in deferred block)
  size_t deferredTransactionsCount_;
  std::chrono::steady_clock::time_point startPointMs_;
  size_t totalDurationMs_;

  std::multiset<size_t> shortestRounds_;
  std::multiset<size_t> longestRounds_;
};

}  // namespace cs
