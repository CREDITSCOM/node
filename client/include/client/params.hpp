#pragma once

//#define MONITOR_NODE
//#define SPAM_MAIN
//#define STARTER
//#define NODE_API
#define AJAX_IFACE
#define CUSTOMER_NODE
//#define FOREVER_ALONE
#define TIME_TO_COLLECT_TRXNS 500
#define TIME_TO_AWAIT_ACTIVITY 300
#define TRX_SLEEP_TIME 50000  // microseconds
#define FAKE_BLOCKS
#ifndef MONITOR_NODE
//#define SPAMMER
#else
#define STATS
#endif
#define SYNCRO
#define MYLOG

#define BOTTLENECKED_SMARTS
#define AJAX_CONCURRENT_API_CLIENTS INT64_MAX
#define BINARY_TCP_API
#define DEFAULT_CURRENCY 1

constexpr auto   SIZE_OF_COMMON_TRANSACTION  = 190;
constexpr double COST_OF_ONE_TRUSTED_PER_DAY = 17;
