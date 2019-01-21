#include "stdafx.h"

#include <csdb/currency.hpp>
#include <algorithm>
#include <apihandler.hpp>
#include <cassert>
#include <client/params.hpp>
#include <csstats.hpp>

namespace csstats {

template <class F>
void csstats::matchPeriod(const Periods& periods, period_t period, F func) {
  for (size_t i = 0; i < periods.size(); ++i) {
    if (period < periods[i]) {
      func(i);
    }
  }
}

StatsPerPeriod csstats::collectStats(const Periods& periods) {
  assert(
      std::is_sorted(std::begin(periods), std::end(periods), [](const Period& l, const Period& r) { return l < r; }));

  cstrace() << " Collecting stats: started";

  auto stats = currentStats_;
  auto startTime = std::chrono::high_resolution_clock::now();
  auto blockHash = blockchain_.getLastHash();

  PeriodStats periodStats;
  periodStats.timeStamp = std::chrono::system_clock::now();

  {
    auto future_last_hash = blockHash;

    if (blockHash.is_empty()) {
      cserror() << "Stats: no bricks in the wall (last hash is empty)";
    }

    while (blockHash != lastHash_ && !blockHash.is_empty()) {
      csdb::Pool pool = blockchain_.loadBlock(blockHash);

      periodStats.poolsCount++;

      std::size_t transactionsCount = pool.transactions_count();
      periodStats.transactionsCount += static_cast<uint32_t>(transactionsCount);

      for (std::size_t i = 0; i < transactionsCount; ++i) {
        const auto& transaction = pool.transaction(csdb::TransactionID(pool.hash(), i));

        if (transaction.user_field(0).is_valid()) {
          ++periodStats.transactionsSmartCount;  // transactionsSmartCount - amount of transactions associated with
        }                                        // smart contracts

        if (is_deploy_transaction(transaction)) {
          ++periodStats.smartContractsCount;
        }

        Currency currency = indexedСurrencies_[transaction.currency().to_string()];

        const auto& amount = transaction.amount();

        periodStats.balancePerCurrency[currency].integral += amount.integral();
        periodStats.balancePerCurrency[currency].fraction += amount.fraction();
      }

      blockHash = pool.previous_hash();
    }

    lastHash_ = future_last_hash;
  }

  // total
  auto& total = stats[PeriodIndex::Total];
  total.poolsCount += periodStats.poolsCount;
  total.transactionsCount += periodStats.transactionsCount;
  total.smartContractsCount += periodStats.smartContractsCount;
  total.transactionsSmartCount += periodStats.transactionsSmartCount;

  for (auto& element : periodStats.balancePerCurrency) {
    total.balancePerCurrency[element.first].integral += element.second.integral;
    total.balancePerCurrency[element.first].fraction += element.second.fraction;
  }

  for (size_t i = 0; i < periods.size(); ++i) {
    assert(updateTimeSec != 0);

    std::size_t index = (periods[i] / updateTimeSec) - 1;

    try {
      auto& lastPeriodStats = statsCut_.at(index);
      auto& stat = stats.at(i);

      // remove last stats
      stat.poolsCount -= lastPeriodStats.poolsCount;
      stat.smartContractsCount -= lastPeriodStats.smartContractsCount;
      stat.transactionsCount -= lastPeriodStats.transactionsCount;
      stat.transactionsSmartCount -= lastPeriodStats.transactionsSmartCount;

      for (auto& element : lastPeriodStats.balancePerCurrency) {
        stat.balancePerCurrency[element.first].integral -= element.second.integral;
        stat.balancePerCurrency[element.first].fraction -= element.second.fraction;
      }

      // add new stats
      stat.poolsCount += periodStats.poolsCount;
      stat.smartContractsCount += periodStats.smartContractsCount;
      stat.transactionsCount += periodStats.transactionsCount;
      stat.transactionsSmartCount += periodStats.transactionsSmartCount;

      for (auto& element : periodStats.balancePerCurrency) {
        stat.balancePerCurrency[element.first].integral += element.second.integral;
        stat.balancePerCurrency[element.first].fraction += element.second.fraction;
      }
    }
    catch (const std::exception& e) {
      cslog() << "STATS> " << e.what();
    }
  }

  // update cuts
  statsCut_.push_front(periodStats);
  statsCut_.pop_back();

  auto finishTime = std::chrono::high_resolution_clock::now();

  using Seconds = std::chrono::seconds;
  Seconds seconds = std::chrono::duration_cast<Seconds>(finishTime - startTime);

  cstrace() << "STATS> Collecting stats: finished (took " << seconds.count() << "s";
  cslog() << "STATS> Stats updated";

  return stats;
}

AllStats csstats::collectAllStats(const Periods& periods) {
  assert(
      std::is_sorted(std::begin(periods), std::end(periods), [](const Period& l, const Period& r) { return l < r; }));

  cstrace() << "STATS>  Collecting All stats: started";

  AllStats stats;
  stats.second.resize(periods.size());

  auto nowGlobal = std::chrono::system_clock::now();
  auto lastTimePoint = nowGlobal - std::chrono::seconds(collectionPeriods[PeriodIndex::Month]);

  for (auto time = nowGlobal; time > lastTimePoint; time -= std::chrono::seconds(updateTimeSec)) {
    PeriodStats cut;
    cut.timeStamp = time;

    stats.first.push_back(cut);
  }

  auto startTime = std::chrono::system_clock::now();
  auto blockHash = blockchain_.getLastHash();
  unsigned int currentCutIndex = 0;
  auto startCutTime = stats.first[currentCutIndex].timeStamp;
  auto endCutTime = stats.first[currentCutIndex + 1].timeStamp;

  auto future_lastHash = blockchain_.getLastHash();

  while (!blockHash.is_empty() && !quit_) {
    const csdb::Pool pool = blockchain_.loadBlock(blockHash);

    auto now = std::chrono::system_clock::now();
    auto poolTime_t = atoll(pool.user_field(0).value<std::string>().c_str()) / 1000;
    auto poolTime = std::chrono::system_clock::from_time_t(poolTime_t);

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

        Currency currency = indexedСurrencies_[transaction.currency().to_string()];

        const auto& amount = transaction.amount();

        periodStats.balancePerCurrency[currency].integral += amount.integral();
        periodStats.balancePerCurrency[currency].fraction += amount.fraction();
      }
    }
    else if ((currentCutIndex + 1) < stats.first.size()) {
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
        const auto& transaction = pool.transaction(csdb::TransactionID(pool.hash(), i));

        if (is_deploy_transaction(transaction))
          ++periodStats.smartContractsCount;

        Currency currency = indexedСurrencies_[transaction.currency().to_string()];

        const auto& amount = transaction.amount();

        periodStats.balancePerCurrency[currency].integral += amount.integral();
        periodStats.balancePerCurrency[currency].fraction += amount.fraction();
      }
    });

    blockHash = pool.previous_hash();
  }

  //lastHash = blockchain.getLastHash();
  lastHash_ = future_lastHash;

  auto finishTime = std::chrono::system_clock::now();
  auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(finishTime - startTime);

  cstrace() << "Collecting All stats: finished (took " << milliseconds.count() << "ms)";

  return stats;
}

csstats::csstats(BlockChain& blockchain)
: blockchain_(blockchain) {
  cstrace() << "STATS> csstats start " << "update interval is " << updateTimeSec << " sec";
#ifndef STATS
  return;
#endif

  ScopedLock lock(mutex_);

  thread_ = std::thread([this]() {
    cstrace() << "STATS> csstats thread started";

    AllStats allStats = collectAllStats(::csstats::collectionPeriods);

    currentStats_ = std::move(allStats.second);
    statsCut_ = std::move(allStats.first);

    assert(currentStats_.size() == collectionPeriods.size());

    for (size_t i = 0; i < currentStats_.size(); ++i)
      currentStats_[i].periodSec = collectionPeriods[i];

    while (!quit_) {
      using namespace std::chrono_literals;

      if (!quit_) {
        auto periods = collectionPeriods;
        periods.pop_back();

        StatsPerPeriod stats = collectStats(periods);

        for (auto& s : stats) {
          std::stringstream ss;
          ss << "Period " << s.periodSec << " collected ";
          ss << s.poolsCount << " pools, " << s.transactionsCount;
          ss << " transactions";

          cstrace() << "STATS> "  << ss.str();
#ifdef LOG_STATS_TO_FILE
          cstrace() << "STATS> "  << ss.str();

          ss.str(std::string());

          ss << "Blockchain size:";
          ss << this->blockchain_.getSize();

          cstrace() << "STATS> " << ss.str();
#endif
          for (auto& t : s.balancePerCurrency) {
            cstrace() << "STATS> "  << "'" << t.first
                      << "' = " << std::to_string(t.second.integral) << "." << std::to_string(t.second.fraction);
          }
        }

        {
          ScopedLock lock(currentStatsMutex_);
          currentStats_ = std::move(stats);
        }
      }

      std::this_thread::sleep_for(std::chrono::seconds(updateTimeSec));
    }

    cstrace() << "STATS> csstats thread stopped";
  });
}

csstats::~csstats() {
  cstrace() << "STATS> csstats stop";

  ScopedLock lock(mutex_);

  quit_ = true;

  if (thread_.joinable()) {
    thread_.join();
  }
}

StatsPerPeriod csstats::getStats() {
  ScopedLock lock(currentStatsMutex_);
  return currentStats_;
}
}  // namespace csstats
