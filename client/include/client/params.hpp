#pragma once

//#define MONITOR_NODE
//#define SPAM_MAIN
//#define STARTER
#define NODE_API
#define AJAX_IFACE
//#define CUSTOMER_NODE
//#define FOREVER_ALONE
#define TIME_TO_COLLECT_TRXNS 500
#define TIME_TO_AWAIT_ACTIVITY 300
#define TRX_SLEEP_TIME 70000 //microseconds
//#define FAKE_BLOCKS
//#define SPAMMER
#define SYNCRO
//#define MYLOG
#ifdef MYLOG
#define CL_G true
#endif
#ifndef MYLOG
#define CL_G false
#endif
#define CLOG(A) if(CL_G) std::cout << __FILE__ << "> " << __func__ << ": " << A << std::endl;
//#define LOG_TRANSACTIONS

#define BOTTLENECKED_SMARTS
#define AJAX_CONCURRENT_API_CLIENTS INT64_MAX
#define BINARY_TCP_API
#define DEFAULT_CURRENCY 1
