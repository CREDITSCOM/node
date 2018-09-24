#ifndef __CACHE_HPP__
#define __CACHE_HPP__

constexpr unsigned cache_linesize = 64;

#define __cacheline_aligned alignas(cache_linesize)

#endif  //__CACHE_H__
