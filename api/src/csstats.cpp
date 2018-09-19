#define TRACE_ENABLED
#include <lib/system/logger.hpp>
#include <algorithm>
#include <cassert>
#include <client/params.hpp>
#include <csdb/currency.h>
#include <csstats.h>

#include <APIHandler.h>

namespace csstats {

template<class F>
void
csstats::matchPeriod(const Periods& periods, period_t period, F func)
{
  for (size_t i = 0; i < periods.size(); ++i) {
    if (period < periods[i])
      func(i);
  }
}

StatsPerPeriod
csstats::collectStats(const Periods& periods)
{
  assert(
    std::is_sorted(std::begin(periods),
                   std::end(periods),
                   [](const Period& l, const Period& r) { return l < r; }));

  TRACE("Collecting stats: started");

  auto startTime = std::chrono::high_resolution_clock::now();

  StatsPerPeriod stats;
  for (auto& period : periods) {
    PeriodStats p;
    p.periodSec = period;

    stats.push_back(p);
  }

  std::string prevHash;

  auto blockHash = blockchain.getLastHash();

  while (!blockHash.is_empty() && !quit) {
    typedef std::vector<csdb::Transaction> Transactions;
    Transactions transactions;

    const csdb::Pool pool = blockchain.loadBlock(blockHash);

    auto now = std::chrono::system_clock::now();
    auto poolTime_t =
      atoll(pool.user_field(0).value<std::string>().c_str()) / 1000;
    auto poolTime = std::chrono::system_clock::from_time_t(poolTime_t);

    std::chrono::seconds poolAgeSec =
      std::chrono::duration_cast<std::chrono::seconds>(now - poolTime);

    matchPeriod(periods, (Period)poolAgeSec.count(), [&stats, &pool](size_t periodIndex) {
      PeriodStats& periodStats = stats[periodIndex];
      periodStats.poolsCount++;

      size_t transactionsCount = pool.transactions_count();
      periodStats.transactionsCount += transactionsCount;
      for (size_t i = 0; i < transactionsCount; ++i) {
        const auto& transaction =
          pool.transaction(csdb::TransactionID(pool.hash(), i));

        TRACE("");

        auto scuf = transaction.user_field(0);

        TRACE("");

        if (scuf.is_valid() &&
            is_smart_deploy(deserialize<api::SmartContractInvocation>(
              scuf.value<std::string>()))) {
          TRACE("");
          ++periodStats.smartContractsCount;
        }

        TRACE("");

        Currency currency = DEFAULT_CURRENCY;

        const auto& amount = transaction.amount();

        periodStats.balancePerCurrency[currency].integral += amount.integral();
        periodStats.balancePerCurrency[currency].fraction += amount.fraction();
      }
    });

	TRACE("");

    blockHash = pool.previous_hash();
  }

  auto finishTime = std::chrono::high_resolution_clock::now();

  std::chrono::seconds seconds =
    std::chrono::duration_cast<std::chrono::seconds>(finishTime - startTime);

  TRACE("Collecting stats: finished (took " << (int)seconds.count() << "s)");

  return stats;
}

csstats::csstats(BlockChain& blockchain, const Config& config)
  : blockchain(blockchain)
{
#ifndef STATS
  return;
#endif

  TRACE(
    "csstats start " << "update interval is " << config.updateIntervalSec << " sec");

  ScopedLock lock(mutex);

  thread = std::thread([=]() {
    TRACE("csstats thread started");

    while (!quit) {
      using namespace std::chrono_literals;

      std::unique_lock<std::mutex> lock(quitCVMutex);

      auto nextUpdateTime = lastUpdateTime + config.updateIntervalSec * 1000ms;

      quitCV.wait_until(lock, nextUpdateTime, [this]() { return quit; });

      if (!quit) {
        const uint32_t secondsPerDay = 24 * 60 * 60;
        StatsPerPeriod stats = collectStats({ secondsPerDay,
                                              secondsPerDay * 7,
                                              secondsPerDay * 30,
                                              secondsPerDay * 365 * 100 });

        for (auto& s : stats) {
          TRACE("Period " <<
                s.periodSec <<
                " collected " <<
                s.poolsCount <<
                " pools, " <<
                s.transactionsCount <<
                " transactions");

          for (auto& t : s.balancePerCurrency) {
            TRACE("'" <<
                  t.first <<
                  "' = " <<
                  std::to_string(t.second.integral) <<
                  "." <<
                  std::to_string(t.second.fraction));
          }
        }

        {
          ScopedLock lock(currentStatsMutex);
          currentStats = std::move(stats);
        }

        lastUpdateTime = std::chrono::system_clock::now();
      }
    }

    TRACE("csstats thread stopped");
  });
}

csstats::~csstats()
{
  TRACE("csstats stop");

  ScopedLock lock(mutex);

  quit = true;
  quitCV.notify_all();

  if (thread.joinable())
    thread.join();
}

StatsPerPeriod
csstats::getStats()
{
  ScopedLock lock(currentStatsMutex);
  return currentStats;
}
}
