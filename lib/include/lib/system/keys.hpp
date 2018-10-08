#ifndef __KEYS_HPP__
#define __KEYS_HPP__

#include <lib/system/hash.hpp>
#include <lib/system/structures.hpp>
#include <lib/system/common.hpp>

inline cs::PublicKey getHashedPublicKey(const char* str) {
  cs::PublicKey result;

  blake2sp(result.data(), BLAKE2_HASH_LENGTH, str, PUBLIC_KEY_LENGTH, nullptr, 0);

  return result;
}


#endif // __KEYS_HPP__
