#ifndef PARAMS_HPP
#define PARAMS_HPP

/**
 *  Please don't commit these three defines
 *  below uncommented.
 */
//#define MONITOR_NODE
//#define WEB_WALLET_NODE

#define NODE_API
#define AJAX_IFACE

#define AJAX_CONCURRENT_API_CLIENTS INT64_MAX
#define BINARY_TCP_API
#define BINARY_TCP_EXECAPI
#define DIAG_API
#define DEFAULT_CURRENCY 1

#if defined(MONITOR_NODE) || defined(WEB_WALLET_NODE)
#define PROFILE_API
#define TOKENS_CACHE
#endif

#ifdef MONITOR_NODE
#define STATS
#endif

// diagnostic output & compatibility checks
#if defined(_MSC_VER)

#if defined(MONITOR_NODE)

#if defined(WEB_WALLET_NODE)
#error Incompatible macros defined: MONITOR_NODE & WEB_WALLET_NODE
#endif
//#pragma message ( "*** Building monitor node" )

#elif defined(WEB_WALLET_NODE)

#if defined(MONITOR_NODE)
#error Incompatible macros defined: WEB_WALLET_NODE & MONITOR_NODE
#endif
//#pragma message ( "*** Building web wallet node" )

#else

//#pragma message ( "*** Building basic node" )

#endif

#endif  // _MSC_VER

#endif
