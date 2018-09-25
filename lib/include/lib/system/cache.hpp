#ifndef __CACHE_HPP__
#define __CACHE_HPP__

constexpr unsigned cache_linesize = 64;

#ifdef _MSC_VER
#define __cacheline_aligned
#else
#define __cacheline_aligned alignas(cache_linesize)
#endif

#endif  //__CACHE_H__
