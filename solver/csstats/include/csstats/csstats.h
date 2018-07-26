#ifndef CSSTATS_H
#define CSSTATS_H

#include <stdint.h>
#include <vector>
#include <unordered_map>

#include <csnode/Blockchain.hpp>

namespace csstats
{


	struct Config
	{
		uint16_t updateIntervalSec = 300;
	};

	void start(Credits::BlockChain*, const Config& config = Config{});
	void stop();

	using Count = uint32_t;

	using Integral = int32_t;
	using Fraction = int64_t;

	struct Amount
	{
		Integral integral = 0;
		Fraction fraction = 0;
	};

	using Balance = Amount;

	using Currency = std::string;

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

	StatsPerPeriod getStats();
}

#endif // CSSTATS_H