#include <csnode/roundstat.hpp>
#include <sstream>
#include <lib/system/logger.hpp>

namespace cs
{

  RoundStat::RoundStat()
    : total_recv_trans(0)
    , total_accepted_trans(0)
    , cnt_deferred_trans(0)
    , total_duration_ms(0)
  {
    t_start_ms = std::chrono::steady_clock::now();
  }

  void RoundStat::onRoundStart(RoundNumber round)
  {
    // minimal statistics, skip 0 & 1 rounds because of possibility extra timeouts
    if(round < 2) {
      t_start_ms = std::chrono::steady_clock::now();
      total_duration_ms = 0;
    }
    else {
      using namespace std::chrono;
      auto new_duration_ms = duration_cast<milliseconds>(steady_clock::now() - t_start_ms).count();
      auto last_round_ms = new_duration_ms - total_duration_ms;
      total_duration_ms = new_duration_ms;
      auto ave_round_ms = total_duration_ms / round;

      //shortest_rounds.insert(last_round_ms);
      //longest_rounds.insert(last_round_ms);

      // TODO: use more intelligent output formatting
      std::ostringstream os;
      constexpr size_t in_minutes = 5 * 60 * 1000;
      constexpr size_t in_seconds = 10 * 1000;
      os << " last round ";
      if(last_round_ms > in_minutes) {
        os << "> " << last_round_ms / 60000 << "min";
      }
      else if(last_round_ms > in_seconds) {
        os << "> " << last_round_ms / 1000 << "sec";
      }
      else {
        os << last_round_ms << "ms";
      }
      os << ", average round ";
      if(ave_round_ms > in_seconds) {
        os << "> " << ave_round_ms / 1000 << "sec";
      }
      else {
        os << ave_round_ms << "ms";
      }
      os << ", " << total_recv_trans << " viewed trans., " << total_accepted_trans << " stored trans.";
      cslog() << os.str();
    }

  }
} // cs
