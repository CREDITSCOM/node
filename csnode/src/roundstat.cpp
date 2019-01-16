#include <csnode/roundstat.hpp>
#include <lib/system/logger.hpp>
#include <sstream>

namespace cs {
RoundStat::RoundStat()
: totalReceivedTransactions_(0)
, totalAcceptedTransactions_(0)
, deferredTransactionsCount_(0)
, totalDurationMs_(0) {
  startPointMs_ = std::chrono::steady_clock::now();
}

void RoundStat::onRoundStart(RoundNumber round) {
  // minimal statistics, skip 0 & 1 rounds because of possibility extra timeouts
  if (round < 2) {
    startPointMs_ = std::chrono::steady_clock::now();
    totalDurationMs_ = 0;
  }
  else {
    using namespace std::chrono;
    auto new_duration_ms = duration_cast<milliseconds>(steady_clock::now() - startPointMs_).count();
    auto last_round_ms = new_duration_ms - totalDurationMs_;
    totalDurationMs_ = new_duration_ms;
    auto ave_round_ms = totalDurationMs_ / round;

    // shortest_rounds.insert(last_round_ms);
    // longest_rounds.insert(last_round_ms);

    // TODO: use more intelligent output formatting
    std::ostringstream os;
    constexpr size_t in_minutes = 5 * 60 * 1000;
    constexpr size_t in_seconds = 10 * 1000;

    os << " last round ";

    if (last_round_ms > in_minutes) {
      os << "> " << last_round_ms / 60000 << "min";
    }
    else if (last_round_ms > in_seconds) {
      os << "> " << last_round_ms / 1000 << "sec";
    }
    else {
      os << last_round_ms << "ms";
    }

    os << ", average round ";

    if (ave_round_ms > in_seconds) {
      os << "> " << ave_round_ms / 1000 << "sec";
    }
    else {
      os << ave_round_ms << "ms";
    }

    os << ", "
      //<< totalReceivedTransactions_ << " viewed transactions, "
      << totalAcceptedTransactions_ << " stored transactions.";
    cslog() << os.str();
  }
}
void RoundStat::onReadBlock(csdb::Pool block, bool * /*should_stop*/)
{
  totalAcceptedTransactions_ += block.transactions_count();
}
}  // namespace cs
