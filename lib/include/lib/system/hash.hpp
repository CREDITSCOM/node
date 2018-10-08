/* Send blaming letters to @yrtimd */
#ifndef __HASH_HPP__
#define __HASH_HPP__

#include <lib/system/structures.hpp>
#include <lib/system/common.hpp>

extern "C" {
  int blake2sp(void* out, size_t outlen, const void* in, size_t inlen, const void* key, size_t keylen);
}

inline cs::Hash getBlake2Hash(const void* data, size_t length) {
  cs::Hash result;
  blake2sp(result.data(), result.size(), data, length, nullptr, 0);
  return result;
}

template <>
inline uint16_t getHashIndex(const cs::Hash& hash) {
  constexpr const size_t border = HASH_LENGTH / 2;

  uint16_t result = 0;
  auto byte = reinterpret_cast<uint8_t*>(&result);

  for (size_t i = 0; i < border; ++i)
    *byte ^= hash[i];

  ++byte;

  for (size_t i = border; i < HASH_LENGTH; ++i)
    *byte ^= hash[i];

  return result;
}


#endif // __HASH_HPP__
