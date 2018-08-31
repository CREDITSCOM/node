#ifndef __LOGGER_HPP__
#define __LOGGER_HPP__
#include <iostream>
#include <string>

#define FLAG_LOG_NOTICE 1
#define FLAG_LOG_WARN 2
#define FLAG_LOG_ERROR 4

#define FLAG_LOG_EVENTS 8

#define FLAG_LOG_PACKETS 16
#define FLAG_LOG_NODES_BUFFER 32

#define FLAG_LOG_DEBUG 64

///////////////////
#define LOG_LEVEL (FLAG_LOG_NOTICE | 0 | FLAG_LOG_ERROR | 0 | FLAG_LOG_PACKETS | (FLAG_LOG_NODES_BUFFER & 0)) | LOG_DEBUG //(FLAG_LOG_PACKETS & 0)
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

#if ( LOG_LEVEL & FLAG_LOG_PACKETS) //false &&
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

#if LOG_LEVEL & FLAG_LOG_DEBUG
#define LOG_DEBUG(TEXT) std::cout << "##" << TEXT << std::endl
#else
#define LOG_DEBUG(TEXT)
#endif

#define SUPER_TIC()

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
