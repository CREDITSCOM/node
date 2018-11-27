#ifndef KEYS_HPP
#define KEYS_HPP

#include <lib/system/common.hpp>
#include <lib/system/hash.hpp>
#include <lib/system/structures.hpp>

inline cs::Hash getHashedPublicKey(const char* str) {
  cs::Hash hash;
  cscrypto::CalculateHash(hash, reinterpret_cast<const uint8_t*>(str), PUBLIC_KEY_LENGTH);

  return hash;
}

#endif  // KEYS_HPP
