#include "stdafx.h"

#include <csdb/currency.h>
#include <csstats.h>
#include <algorithm>
#include <cassert>
#include <client/params.hpp>
#include <csdb/currency.h>
#include <csstats.h>

#include <APIHandler.h>

namespace csstats {

template <class F>
void csstats::matchPeriod(const Periods& periods, period_t period, F func) {
  for (size_t i = 0; i < periods.size(); ++i) {
    if (period < periods[i])
      func(i);
  }
}

StatsPerPeriod csstats::collectStats(const Periods& periods) {
  assert(
      std::is_sorted(std::begin(periods), std::end(periods), [](const Period& l, const Period& r) { return l < r; }));

  TRACE("Collecting stats: started");

  auto stats = currentStats;
  auto startTime = std::chrono::high_resolution_clock::now();
  auto blockHash = blockchain.getLastWrittenHash();

  PeriodStats periodStats;
  periodStats.timeStamp = std::chrono::system_clock::now();

  {
    auto future_last_hash = blockHash;

    if (blockHash.is_empty())
      LOG_ERROR("Stats: no bricks in the wall (last hash is empty)");

    while (blockHash != lastHash && !blockHash.is_empty()) {
      csdb::Pool pool = blockchain.loadBlock(blockHash);

      periodStats.poolsCount++;

      std::size_t transactionsCount = pool.transactions_count();
      periodStats.transactionsCount += static_cast<uint32_t>(transactionsCount);

      for (std::size_t i = 0; i < transactionsCount; ++i) {
        const auto& transaction = pool.transaction(csdb::TransactionID(pool.hash(), i));

        if(transaction.user_field(0).is_valid())
          ++periodStats.transactionsSmartCount; //transactionsSmartCount - amount of transactions associated with smart contracts

        if (is_deploy_transaction(transaction))
          ++periodStats.smartContractsCount;

        Currency currency =
          currencies_indexed[transaction.currency().to_string()];

        const auto& amount = transaction.amount();

        periodStats.balancePerCurrency[currency].integral += amount.integral();
        periodStats.balancePerCurrency[currency].fraction += amount.fraction();
      }

      blockHash = pool.previous_hash();
    }

    lastHash = future_last_hash;
  }

  // total
  auto& total = stats[PeriodIndex::Total];
  total.poolsCount += periodStats.poolsCount;
  total.transactionsCount += periodStats.transactionsCount;
  total.smartContractsCount += periodStats.smartContractsCount;

  for (auto& element : periodStats.balancePerCurrency) {
    total.balancePerCurrency[element.first].integral += element.second.integral;
    total.balancePerCurrency[element.first].fraction += element.second.fraction;
  }

  for (size_t i = 0; i < periods.size(); ++i) {
    assert(updateTimeSec != 0);

    std::size_t index = (periods[i] / updateTimeSec) - 1;

    try {
      auto& lastPeriodStats = statsCut.at(index);
      auto& stat = stats.at(i);

      // remove last stats
      stat.poolsCount -= lastPeriodStats.poolsCount;
      stat.smartContractsCount -= lastPeriodStats.smartContractsCount;
      stat.transactionsCount -= lastPeriodStats.transactionsCount;

      for (auto& element : lastPeriodStats.balancePerCurrency) {
        stat.balancePerCurrency[element.first].integral -=
          element.second.integral;
        stat.balancePerCurrency[element.first].fraction -=
          element.second.fraction;
      }

      // add new stats
      stat.poolsCount += periodStats.poolsCount;
      stat.smartContractsCount += periodStats.smartContractsCount;
      stat.transactionsCount += periodStats.transactionsCount;

      for (auto& element : periodStats.balancePerCurrency) {
        stat.balancePerCurrency[element.first].integral +=
          element.second.integral;
        stat.balancePerCurrency[element.first].fraction +=
          element.second.fraction;
      }
    } catch (const std::exception& e) {
      cslog() << e.what();
    }
  }

  // update cuts
  statsCut.push_front(periodStats);
  statsCut.pop_back();

  auto finishTime = std::chrono::high_resolution_clock::now();

  using Seconds = std::chrono::seconds;
  Seconds seconds = std::chrono::duration_cast<Seconds>(finishTime - startTime);

  TRACE("Collecting stats: finished (took ", (int)seconds.count(), "s)");
  cslog() << "Stats updated";

  return stats;
}

AllStats
csstats::collectAllStats(const Periods& periods)
{
  assert(
    std::is_sorted(std::begin(periods),
                   std::end(periods),
                   [](const Period& l, const Period& r) { return l < r; }));

  TRACE("Collecting All stats: started");

  AllStats stats;
  stats.second.resize(periods.size());

  auto nowGlobal = std::chrono::system_clock::now();
  auto lastTimePoint =
    nowGlobal - std::chrono::seconds(collectionPeriods[PeriodIndex::Month]);

  for (auto time = nowGlobal; time > lastTimePoint;
       time -= std::chrono::seconds(updateTimeSec)) {
    PeriodStats cut;
    cut.timeStamp = time;

    stats.first.push_back(cut);
  }

  auto startTime = std::chrono::system_clock::now();
  auto blockHash = blockchain.getLastHash();
  unsigned int currentCutIndex = 0;
  auto startCutTime = stats.first[currentCutIndex].timeStamp;
  auto endCutTime = stats.first[currentCutIndex + 1].timeStamp;

  while (!blockHash.is_empty() && !quit) {
    const csdb::Pool pool = blockchain.loadBlock(blockHash);

    auto now        = std::chrono::system_clock::now();
    auto poolTime_t = atoll(pool.user_field(0).value<std::string>().c_str()) / 1000;
    auto poolTime   = std::chrono::system_clock::from_time_t(poolTime_t);

    using Seconds = std::chrono::seconds;
    Seconds poolAgeSec = std::chrono::duration_cast<Seconds>(now - poolTime);

    if (startCutTime <= poolTime && poolTime < endCutTime) {
      PeriodStats& periodStats = stats.first[currentCutIndex];
      ++periodStats.poolsCount;

      size_t transactionsCount = pool.transactions_count();
      periodStats.transactionsCount += (uint32_t)transactionsCount;

      for (size_t i = 0; i < transactionsCount; ++i) {
        const auto& transaction = pool.transaction(csdb::TransactionID(pool.hash(), i));

        if (is_deploy_transaction(transaction)) {
          ++periodStats.smartContractsCount;
        }

        Currency currency =
          currencies_indexed[transaction.currency().to_string()];

        const auto& amount = transaction.amount();

        periodStats.balancePerCurrency[currency].integral += amount.integral();
        periodStats.balancePerCurrency[currency].fraction += amount.fraction();
      }
    } else if ((currentCutIndex + 1) < stats.first.size()) {
      startCutTime = stats.first[currentCutIndex].timeStamp;
      endCutTime = stats.first[currentCutIndex + 1].timeStamp;

      ++currentCutIndex;
      continue;
    }

    matchPeriod(periods, (Period)poolAgeSec.count(), [&](size_t periodIndex) {
      PeriodStats& periodStats = stats.second[periodIndex];
      periodStats.poolsCount++;

      size_t transactionsCount = pool.transactions_count();
      periodStats.transactionsCount += (uint32_t)transactionsCount;

      for (size_t i = 0; i < transactionsCount; ++i) {
        const auto& transaction =
          pool.transaction(csdb::TransactionID(pool.hash(), i));

        if (is_deploy_transaction(transaction))
          ++periodStats.smartContractsCount;

        Currency currency =
          currencies_indexed[transaction.currency().to_string()];

        const auto& amount = transaction.amount();

        periodStats.balancePerCurrency[currency].integral += amount.integral();
        periodStats.balancePerCurrency[currency].fraction += amount.fraction();
      }
    });

    blockHash = pool.previous_hash();
  }

  lastHash = blockchain.getLastWrittenHash();

  auto finishTime = std::chrono::system_clock::now();
  auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
    finishTime - startTime);

  TRACE("Collecting All stats: finished (took ", milliseconds.count(), "ms)");

  return stats;
}

csstats::csstats(BlockChain& blockchain)
  : blockchain(blockchain)
{
  TRACE("csstats start ", "update interval is ", updateTimeSec, " sec");

#ifndef MONITOR_NODE
  return;
#endif

  ScopedLock lock(mutex);

  thread = std::thread([this]() {
    TRACE("csstats thread started");

    AllStats allStats = collectAllStats(::csstats::collectionPeriods);

    currentStats = std::move(allStats.second);
    statsCut = std::move(allStats.first);

    assert(currentStats.size() == collectionPeriods.size());

    for (auto i = 0ull; i < currentStats.size(); ++i)
      currentStats[i].periodSec = collectionPeriods[i];

    while (!quit) {
      using namespace std::chrono_literals;

      if (!quit) {
        auto periods = collectionPeriods;
        periods.pop_back();

        StatsPerPeriod stats = collectStats(periods);

        for (auto& s : stats) {
          std::stringstream ss;
          ss << "Period " << s.periodSec << " collected ";
          ss << s.poolsCount << " pools, " << s.transactionsCount;
          ss << " transactions";

          TRACE(ss.str());
#ifdef LOG_STATS_TO_FILE
          TRACE(ss.str());

          ss.str(std::string());

          ss << "Blockchain size:";
          ss << this->blockchain.getSize();

          TRACE(ss.str());
#endif
          for (auto& t : s.balancePerCurrency) {
            TRACE("'",
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
      }

      std::this_thread::sleep_for(std::chrono::seconds(updateTimeSec));
    }

    TRACE("csstats thread stopped");
  });
}

csstats::~csstats() {
  TRACE("csstats stop");

  ScopedLock lock(mutex);

  quit = true;

  if (thread.joinable())
    thread.join();
}

StatsPerPeriod csstats::getStats() {
  ScopedLock lock(currentStatsMutex);
  return currentStats;
}
}  // namespace csstats
