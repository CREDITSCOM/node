#ifndef __LOGGER_HPP__
#define __LOGGER_HPP__
#include <iostream>
#include <string>
#include <sstream>
#include <ctime>

#define FLAG_LOG_NOTICE 1
#define FLAG_LOG_WARN 2
#define FLAG_LOG_ERROR 4

#define FLAG_LOG_EVENTS 8

#define FLAG_LOG_PACKETS 16
#define FLAG_LOG_NODES_BUFFER 32

#define FLAG_LOG_DEBUG 64

///////////////////
#define LOG_LEVEL (FLAG_LOG_NOTICE | FLAG_LOG_WARN | FLAG_LOG_ERROR | FLAG_LOG_EVENTS | (FLAG_LOG_DEBUG & 0))
///////////////////

#if LOG_LEVEL & FLAG_LOG_NOTICE
#define LOG_NOTICE(TEXT) std::cout << "[NOTICE] " << TEXT << std::endl
#else
#define LOG_NOTICE(TEXT)
#endif

#if LOG_LEVEL & FLAG_LOG_WARN
#define LOG_WARN(TEXT) std::cout << "[WARNING] " << TEXT << std::endl
#else
#define LOG_WARN(TEXT)
#endif

#if LOG_LEVEL & FLAG_LOG_ERROR
#define LOG_ERROR(TEXT) std::cout << "[ERROR] " << TEXT << std::endl
#else
#define LOG_ERROR(TEXT)
#endif

#if LOG_LEVEL & FLAG_LOG_EVENTS
#define LOG_EVENT(TEXT) std::cout << TEXT << std::endl
#else
#define LOG_EVENT(TEXT)
#endif

#if (false && LOG_LEVEL & FLAG_LOG_PACKETS)
#define LOG_IN_PACK(DATA, SIZE) std::cout << "-!> " << byteStreamToHex((const char*)(DATA), (SIZE)) << std::endl
#define LOG_OUT_PACK(DATA, SIZE) std::cout << "<!- " << byteStreamToHex((const char*)(DATA), (SIZE)) << std::endl
#else
#define LOG_IN_PACK(DATA, SIZE)
#define LOG_OUT_PACK(DATA, SIZE)
#endif

#if LOG_LEVEL & FLAG_LOG_NODES_BUFFER
#define LOG_NODESBUF_PUSH(ENDPOINT) std::cout << "[+] " << (ENDPOINT).address().to_string() << ":" << (ENDPOINT).port() << std::endl
#define LOG_NODESBUF_POP(ENDPOINT) std::cout << "[-] " << (ENDPOINT).address().to_string() << ":" << (ENDPOINT).port() << std::endl
#else
#define LOG_NODESBUF_PUSH(ENDPOINT)
#define LOG_NODESBUF_POP(ENDPOINT)
#endif

#if (LOG_LEVEL & FLAG_LOG_DEBUG)
#define LOG_DEBUG(TEXT) std::cout << "##" << TEXT << std::endl
#else
#define LOG_DEBUG(TEXT)
#endif

#ifdef TRACE_ENABLER
#define TRACE_ENABLED 1
#else
#define TRACE_ENABLED 0
#endif

extern thread_local bool trace;
#if LOG_LEVEL & FLAG_TRACE & -TRACE_ENABLED
#define TRACE(PRINT_ARGS)                                                      \
  do {                                                                         \
    if (!trace)                                                                \
      break;                                                                   \
    std::ostringstream strstream;                                              \
    std::clock_t uptime = std::clock() / (CLOCKS_PER_SEC / 1000);              \
    std::clock_t ss = uptime / 1000;                                           \
    std::clock_t ms = uptime % 1000;                                           \
    std::clock_t mins = ss / 60;                                               \
    ss %= 60;                                                                  \
    std::clock_t hh = mins / 60;                                               \
    mins %= 60;                                                                \
    char buf[16];                                                              \
    snprintf(buf, sizeof(buf), "[%02d:%02d:%02d.%03d]", hh, mins, ss, ms);     \
    strstream << buf << " " << std::this_thread::get_id() << "|\t" << __FILE__ \
              << ":" << __func__ << ":" << __LINE__;                           \
    strstream << " " << PRINT_ARGS << std::endl;                               \
    std::clog << strstream.str();                                              \
  } while (0)
#else
#define TRACE(PRINT_ARGS) [&]() -> decltype(auto) {}()
#endif

static inline std::string byteStreamToHex(const char* stream, const size_t length) {
  static std::string map = "0123456789ABCDEF";

  std::string result;
  result.reserve(length * 2);

  for (size_t i = 0; i < length; ++i) {
    result.push_back(map[(uint8_t)(stream[i]) >> 4]);
    result.push_back(map[(uint8_t)(stream[i]) & (uint8_t)15]);
  }

  return result;
}

#endif // __LOGGER_HPP__
