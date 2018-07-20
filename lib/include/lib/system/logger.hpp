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
#define LOG_LEVEL (FLAG_LOG_NOTICE | FLAG_LOG_WARN | FLAG_LOG_ERROR | FLAG_LOG_EVENTS | (FLAG_LOG_PACKETS & 0) | (FLAG_LOG_NODES_BUFFER & 0)) | LOG_DEBUG
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

#if LOG_LEVEL & FLAG_LOG_PACKETS
#define LOG_IN_PACK(PACKET, SIZE) std::cout << "-> [" << (SIZE - Packet::headerLength()) << "] " << (int)(PACKET)->command << " : " << (int)(PACKET)->subcommand << " " << byteStreamToHex((PACKET)->HashBlock, hash_length) << std::endl
#define LOG_OUT_PACK(PACKET, SIZE) std::cout << "<- [" << (SIZE - Packet::headerLength()) << "] " << (int)(PACKET)->command << " : " << (int)(PACKET)->subcommand << " " << byteStreamToHex((PACKET)->HashBlock, hash_length) << std::endl
#else
#define LOG_IN_PACK(PACKET, SIZE)
#define LOG_OUT_PACK(PACKET, SIZE)
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

#endif // __LOGGER_HPP__
