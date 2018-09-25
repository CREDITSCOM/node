#ifndef CSSTATS_H
#define CSSTATS_H

#include <atomic>
#include <csnode/blockchain.hpp>
#include <cstdint>
#include <deque>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#define NO_STATS_TEST
#define NO_FAST_UPDATE
#define LOG_STATS_TO_FILE

namespace csstats {

using Period = uint32_t;
using Periods = std::vector<uint32_t>;

using Count = uint32_t;

using Integral = int32_t;
using Fraction = int64_t;

struct Amount
{
  Integral integral = 0;
  Fraction fraction = 0;
};

using Balance = Amount;
using Currency = uint8_t;

struct TotalAmount
{
  int64_t integral = 0;
  int64_t fraction = 0;
};

using BalancePerCurrency = std::unordered_map<Currency, TotalAmount>;
using TimeStamp = std::chrono::system_clock::time_point;

struct PeriodStats
{
  Period periodSec = 0;
  Count poolsCount = 0;
  Count transactionsCount = 0;
  BalancePerCurrency balancePerCurrency;
  Count smartContractsCount = 0;
  TimeStamp timeStamp;
};

using StatsPerPeriod = std::vector<PeriodStats>;
using StatsCut = std::deque<PeriodStats>;
using AllStats = std::pair<StatsCut, StatsPerPeriod>;

enum PeriodIndex
{
  Day = 0,
  Week,
  Month,
  Total,

  PeriodsCount
};

#ifdef NO_FAST_UPDATE
const uint32_t updateTimeSec = 3;
#else
const uint32_t updateTimeSec = 30;
#endif
const uint32_t secondsPerDay = 24 * 60 * 60;
const Periods collectionPeriods = { secondsPerDay,
                                    secondsPerDay * 7,
                                    secondsPerDay * 30,
                                    secondsPerDay * 365 * 100 };

class csstats
{
public:
#ifdef NDEBUG
  inline void Log() {}

  template<typename T, typename... Args>
  inline void Log(T&& t, Args... args)
  {}
#else

  inline void Log() { std::cout << std::endl; }

  template<typename T, typename... Args>
  inline void Log(T&& t, Args... args)
  {
    std::cout << t;
    Log(args...);
  }
#endif
  csstats(BlockChain& blockchain);

  StatsPerPeriod getStats();

  ~csstats();

private:
  std::thread thread;

  std::mutex mutex;
  using ScopedLock = std::lock_guard<std::mutex>;
  std::atomic<bool> quit = { false };

  StatsPerPeriod currentStats;
  StatsCut statsCut;
  csdb::PoolHash lastHash;

  std::mutex currentStatsMutex;
  std::chrono::system_clock::time_point lastUpdateTime =
    std::chrono::system_clock::from_time_t(0);

  BlockChain& blockchain;

  StatsPerPeriod collectStats(const Periods& periods);
  AllStats collectAllStats(const Periods& periods);

  template<class F>
  void matchPeriod(const Periods& periods, uint32_t period, F func);

  std::map<std::string, Currency> currencies_indexed = { { "CS", 1 } };

#ifndef NO_STATS_TEST
  const std::size_t testMaxTransactionCount = 5000;
  const std::size_t testPoolCount = 100;
  const std::size_t testWritePeriodSec = 20;
  std::size_t testSequence = 0;

  void startTests();
  void testStats();
  std::size_t valueGenerator(std::size_t max);
#endif
};
}

#endif // CSSTATS_H
