#pragma once

#include <lib/system/common.hpp>
#include <chrono>
#include <set>

namespace cs
{

  constexpr size_t MaxStoredDurations = 1000;

  class RoundStat
  {
    public:

      RoundStat();

      void onRoundStart(cs::RoundNumber round);


      // amount of transactions received (to verify or not or to ignore)
      size_t total_recv_trans;
      // amount of accepted transactions (stored in blockchain)
      size_t total_accepted_trans;
      // amount of deferred transactions (in deferred block)
      size_t cnt_deferred_trans;
      std::chrono::steady_clock::time_point t_start_ms;
      size_t total_duration_ms;

      std::multiset<size_t> shortest_rounds;
      std::multiset<size_t> longest_rounds;
  };

} // cs
