#ifndef KEYS_HPP
#define KEYS_HPP

#include <lib/system/common.hpp>
#include <lib/system/hash.hpp>
#include <lib/system/structures.hpp>

inline cs::PublicKey getHashedPublicKey(const char* str) {
  cs::PublicKey result;

  blake2sp(result.data(), BLAKE2_HASH_LENGTH, str, PUBLIC_KEY_LENGTH, nullptr, 0);

  return result;
}

#endif  // KEYS_HPP
