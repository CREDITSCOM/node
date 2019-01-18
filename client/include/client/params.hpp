#ifndef PARAMS_HPP
#define PARAMS_HPP

#define MONITOR_NODE
//#define WEB_WALLET_NODE
#define NODE_API
#define AJAX_IFACE

#define AJAX_CONCURRENT_API_CLIENTS INT64_MAX
#define BINARY_TCP_API
#define DEFAULT_CURRENCY 1

#if defined(MONITOR_NODE) || defined(WEB_WALLET_NODE)
  #define TRANSACTIONS_INDEX
  #define TOKENS_CACHE
#else
//  #define SPAMMER
#endif

#ifdef MONITOR_NODE
  #define STATS
#endif

#endif
