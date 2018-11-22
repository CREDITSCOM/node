#ifndef CACHE_HPP
#define CACHE_HPP

constexpr unsigned cache_linesize = 64;

#ifdef _MSC_VER
#define __cacheline_aligned
#else
#define __cacheline_aligned alignas(cache_linesize)
#endif

#endif  // CACHE_H
