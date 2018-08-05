#include "csstats/csstats.h"

#include <mutex>
#include <thread>
#include <iostream>
#include <chrono>
#include <algorithm>
#include <cassert>
#include <condition_variable>

#include <csconnector/csconnector.h>

#include <csdb/csdb.h>
#include <csdb/storage.h>
#include <csdb/pool.h>
#include <csdb/currency.h>
#include <csdb/transaction.h>
#include <csdb/amount.h>
#include <csdb/address.h>

namespace csstats
{
	namespace detail
	{
		std::thread thread;

		std::mutex mutex;
		using ScopedLock = std::lock_guard<std::mutex>;

		std::condition_variable quitCV;
		std::mutex quitCVMutex;
		bool quit = false;

		StatsPerPeriod currentStats;
		std::mutex currentStatsMutex;
		std::chrono::system_clock::time_point lastUpdateTime = std::chrono::system_clock::from_time_t(0);

		using Period = uint32_t;
		using Periods = std::vector<uint32_t>;

		// NOTE: Code duplication with other libraries
		namespace
		{
#ifdef NDEBUG
			inline void Log() {  }

			template <typename T, typename... Args>
			inline void Log(T t, Args... args)
			{
			}
#else
			inline void Log() {	std::cout << std::endl;	}

			template <typename T, typename... Args>
			inline void Log(T t, Args... args)
			{
				std::cout << t;
				Log(args...);
			}
#endif
		}

		template <class F>
		void matchPeriod(const Periods& periods, uint32_t period, F func)
		{
			for (size_t i = 0; i < periods.size(); ++i)
			{
				if (period < periods[i])
					func(i);
			}
		}

		StatsPerPeriod collectStats(Credits::BlockChain* blockchain, const Periods& periods)
		{
			assert(std::is_sorted(std::begin(periods), std::end(periods), [](const Period& l, const Period& r) { return l < r; }));

			Log("Collecting stats: started");

			auto startTime = std::chrono::high_resolution_clock::now();

			StatsPerPeriod stats;
			for (auto& period : periods)
			{
				PeriodStats p;
				p.periodSec = period;

				stats.push_back(p);
			}

			std::string prevHash;
//			std::string hash = csdb::GetHeadHash();

          //  assert( storage.isOpen() );

			auto blockHash = blockchain->getLastHash();

			while (!blockHash.is_empty() && !quit)
			{
                typedef std::vector<csdb::Transaction> Transactions;
				Transactions transactions;

				time_t timeMs = 0;

//				csdb::GetPool(&hash, &prevHash, &transactions, &timeMs, nullptr);
//                const csdb::PoolHash poolHash;
				//size_t sz;
				const csdb::Pool pool = blockchain->loadBlock(blockHash);

				time_t timeSec = timeMs / 1000;

				auto now = std::chrono::system_clock::now();
				auto poolTime_t = atoll(pool.user_field(0).value<std::string>().c_str()) / 1000;
				auto poolTime = std::chrono::system_clock::from_time_t(poolTime_t);

				using Seconds = std::chrono::seconds;
				Seconds poolAgeSec = std::chrono::duration_cast<Seconds>(now - poolTime);

				matchPeriod(periods, (Period)poolAgeSec.count(), [&](size_t periodIndex)
				{
					PeriodStats& periodStats = stats[periodIndex];
					periodStats.poolsCount++;

					size_t transactionsCount = pool.transactions_count();
					periodStats.transactionsCount += transactionsCount;
                    for (size_t i = 0; i < transactionsCount; ++i)
					{
                        const auto& transaction = pool.transaction( csdb::TransactionID(pool.hash(), i) );

						if (transaction.user_field(0).is_valid())
							++periodStats.smartContractsCount;

                        Currency currency = transaction.currency().to_string();

                        const auto& amount = transaction.amount();

                        periodStats.balancePerCurrency[currency].integral += amount.integral();
                        periodStats.balancePerCurrency[currency].fraction += amount.fraction();
					}
				});

				blockHash = pool.previous_hash();
			}

			auto finishTime = std::chrono::high_resolution_clock::now();

			using Seconds = std::chrono::seconds;
			Seconds seconds = std::chrono::duration_cast<Seconds>(finishTime - startTime);

			Log("Collecting stats: finished (took ", (int)seconds.count(), "s)");

			return stats;
		}
	}

	using namespace detail;

	void StatsGet(api::StatsGetResult& _return)
	{
		csstats::StatsPerPeriod stats = getStats();

		for (auto& s : stats)
		{
			api::PeriodStats ps = {};
			ps.periodDuration = s.periodSec;
			ps.poolsCount = s.poolsCount;
			ps.transactionsCount = s.transactionsCount;
			ps.smartContractsCount = s.smartContractsCount;

			for (auto& t : s.balancePerCurrency)
			{
				api::CumulativeAmount amount;
				amount.integral = t.second.integral;
				amount.fraction = t.second.fraction;
				ps.balancePerCurrency[t.first] = amount;
			}

			_return.stats.push_back(ps);
		}
	}

	void start(Credits::BlockChain* blockchain, const Config& config /* = Config{}*/)
	{
          return;
		Log("csstats start ", "update interval is ", config.updateIntervalSec, " sec");

		ScopedLock lock(mutex);

		thread = std::thread(
			[=]()
			{
				Log("csstats thread started");

				while ( !quit )
				{
					using namespace std::chrono_literals;

					std::unique_lock<std::mutex> lock(quitCVMutex);

					auto nextUpdateTime = lastUpdateTime + config.updateIntervalSec * 1000ms;

					quitCV.wait_until(lock, nextUpdateTime, [](){ return quit; });

					if ( !quit )
					{
						const uint32_t secondsPerDay = 24 * 60 * 60;
						StatsPerPeriod stats = collectStats(blockchain,{ secondsPerDay, secondsPerDay * 7, secondsPerDay * 30, secondsPerDay * 365 * 100 });

						for ( auto& s : stats )
						{
							Log("Period ", s.periodSec, " collected ", s.poolsCount, " pools, ", s.transactionsCount, " transactions");

							for ( auto& t : s.balancePerCurrency )
							{
								Log("'", t.first, "' = ", std::to_string(t.second.integral), ".", std::to_string(t.second.fraction));
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

		csconnector::registerHandler<csconnector::Commands::StatsGet>(StatsGet);
	}

	void stop()
	{
		Log("csstats stop");

		ScopedLock lock(mutex);

		csconnector::unregisterHandler<csconnector::Commands::StatsGet>();

		quit = true;
		quitCV.notify_all();

		if ( thread.joinable() )
			thread.join();
	}

	StatsPerPeriod getStats()
	{
		ScopedLock lock(currentStatsMutex);
		return currentStats;
	}
}
