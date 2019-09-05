/* Send blaming letters to @yrtimd */
#ifndef HASH_HPP
#define HASH_HPP

#include <lib/system/common.hpp>
#include "utils.hpp"

inline cs::Hash generateHash(const void* data, size_t length) {
    return cscrypto::calculateHash(reinterpret_cast<const uint8_t*>(data), length);
}

template <>
inline uint16_t getHashIndex(const cs::Hash& hash) {
    constexpr const size_t border = kHashLength / 2;

    uint16_t result = 0;
    auto byte = reinterpret_cast<uint8_t*>(&result);

    for (size_t i = 0; i < border; ++i) {
        *byte ^= hash[i];
    }

    ++byte;

    for (size_t i = border; i < kHashLength; ++i) {
        *byte ^= hash[i];
    }

    return result;
}

template <>
inline uint32_t getHashIndex(const cs::Hash& hash) {
    return *reinterpret_cast<const uint32_t *>(hash.data());
}

template<int N>
uint32_t MurmurHash2(const uint8_t* key) {
  constexpr uint32_t m = 0x5bd1e995;
  constexpr uint32_t seed = 0;
  constexpr int r = 24;

  unsigned int h = seed ^ N;

  const uint8_t* data = key;
  unsigned int k;

  size_t len = N;
  while (len >= 4)
  {
      k  = data[0];
      k |= data[1] << 8;
      k |= data[2] << 16;
      k |= data[3] << 24;

      k *= m;
      k ^= k >> r;
      k *= m;

      h *= m;
      h ^= k;

      data += 4;
      len -= 4;
  }

  switch (len)
  {
    case 3:
      h ^= data[2] << 16;
    case 2:
      h ^= data[1] << 8;
    case 1:
      h ^= data[0];
      h *= m;
  };

  h ^= h >> 13;
  h *= m;
  h ^= h >> 15;

  return h;
}

#endif  // HASH_HPP
