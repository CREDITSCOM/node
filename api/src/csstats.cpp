#include <algorithm>
#include <cassert>
#include <csdb/currency.h>
#include <csstats.h>
#include <client/params.hpp>

namespace csstats {

template<class F>
void
csstats::matchPeriod(const Periods& periods, uint32_t period, F func)
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

    Log("Collecting stats: started");

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

        using Seconds = std::chrono::seconds;
        Seconds poolAgeSec =
          std::chrono::duration_cast<Seconds>(now - poolTime);

        matchPeriod(
          periods, (Period)poolAgeSec.count(), [&](size_t periodIndex) {
              PeriodStats& periodStats = stats[periodIndex];
              periodStats.poolsCount++;

              size_t transactionsCount = pool.transactions_count();
              periodStats.transactionsCount += transactionsCount;
              for (size_t i = 0; i < transactionsCount; ++i) {
                  const auto& transaction =
                    pool.transaction(csdb::TransactionID(pool.hash(), i));

                  if (transaction.user_field(0).is_valid())
                      ++periodStats.smartContractsCount;

                  Currency currency = DEFAULT_CURRENCY;

                  const auto& amount = transaction.amount();

                  periodStats.balancePerCurrency[currency].integral +=
                    amount.integral();
                  periodStats.balancePerCurrency[currency].fraction +=
                    amount.fraction();
              }
          });

        blockHash = pool.previous_hash();
    }

    auto finishTime = std::chrono::high_resolution_clock::now();

    using Seconds = std::chrono::seconds;
    Seconds seconds =
      std::chrono::duration_cast<Seconds>(finishTime - startTime);

    Log("Collecting stats: finished (took ", (int)seconds.count(), "s)");

    return stats;
}

csstats::csstats(BlockChain& blockchain, const Config& config)
  : blockchain(blockchain)
{
#ifndef STATS
    return;
#endif

    Log("csstats start ",
        "update interval is ",
        config.updateIntervalSec,
        " sec");

    ScopedLock lock(mutex);

    thread = std::thread([=]() {
        Log("csstats thread started");

        while (!quit) {
            using namespace std::chrono_literals;

            std::unique_lock<std::mutex> lock(quitCVMutex);

            auto nextUpdateTime =
              lastUpdateTime + config.updateIntervalSec * 1000ms;

            quitCV.wait_until(lock, nextUpdateTime, [this]() { return quit; });

            if (!quit) {
                const uint32_t secondsPerDay = 24 * 60 * 60;
                StatsPerPeriod stats =
                  collectStats({ secondsPerDay,
                                 secondsPerDay * 7,
                                 secondsPerDay * 30,
                                 secondsPerDay * 365 * 100 });

                for (auto& s : stats) {
                    Log("Period ",
                        s.periodSec,
                        " collected ",
                        s.poolsCount,
                        " pools, ",
                        s.transactionsCount,
                        " transactions");

                    for (auto& t : s.balancePerCurrency) {
                        Log("'",
                            t.first,
                            "' = ",
                            std::to_string(t.second.integral),
                            ".",
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

        Log("csstats thread stopped");
    });
}

csstats::~csstats()
{
    Log("csstats stop");

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
