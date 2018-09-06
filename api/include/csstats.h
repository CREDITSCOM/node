#ifndef CSSTATS_H
#define CSSTATS_H

#include <cstdint>
#include <vector>
#include <string>
#include <unordered_map>
#include <thread>
#include <condition_variable>

#include <csnode/blockchain.hpp>

namespace csstats {
    struct Config
    {
        uint16_t updateIntervalSec = {300};
    };

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

    using Currency = int8_t;

    struct TotalAmount
    {
        int64_t integral = 0;
        int64_t fraction = 0;
    };
    using BalancePerCurrency = std::unordered_map<Currency, TotalAmount>;

    struct PeriodStats
    {
        uint32_t periodSec = 0;
        Count poolsCount = 0;
        Count transactionsCount = 0;
        BalancePerCurrency balancePerCurrency;
        Count smartContractsCount = 0;
    };

    using StatsPerPeriod = std::vector<PeriodStats>;

    class csstats
    {
    public:

#ifdef NDEBUG
        inline void Log() {  }

                        template <typename T, typename... Args>
                        inline void Log(T, Args...)
                        {
                        }
#else

        inline void Log() {	std::cerr << std::endl;	}

        template <typename T, typename... Args>
        inline void Log(T t, Args... args)
        {
            std::cerr << t;
            Log(args...);
        }
#endif

		csstats(BlockChain &blockchain, const Config &config = Config{});

        StatsPerPeriod getStats();

        ~csstats();

    private:
        std::thread thread;

        std::mutex mutex;
        using ScopedLock = std::lock_guard<std::mutex>;

        std::condition_variable quitCV;
        std::mutex quitCVMutex;
        bool quit = false;

        StatsPerPeriod currentStats;
        std::mutex currentStatsMutex;
        std::chrono::system_clock::time_point lastUpdateTime = std::chrono::system_clock::from_time_t(0);

        BlockChain &blockchain;

        StatsPerPeriod collectStats(const Periods &periods);

        template<class F>
        void matchPeriod(const Periods &periods, uint32_t period, F func);
    };
}

#endif // CSSTATS_H
