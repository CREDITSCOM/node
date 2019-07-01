#ifndef CACHE_HPP
#define CACHE_HPP

#include <new>

#ifdef __cpp_lib_hardware_interference_size
constexpr unsigned kCacheLineSize = std::hardware_constructive_interference_size;
#else
constexpr unsigned kCacheLineSize = 64;
#endif

#define __cacheline_aligned alignas(kCacheLineSize)

#endif  // CACHE_HPP
