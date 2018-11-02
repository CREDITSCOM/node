#ifndef __KEYS_HPP__
#define __KEYS_HPP__
#include "hash.hpp"
#include "structures.hpp"

const size_t BLAKE2_HASH_LENGTH = 32;
const size_t PUBLIC_KEY_LENGTH = 32;
const size_t PRIVATE_KEY_LENGTH = 64;

typedef FixedString<PUBLIC_KEY_LENGTH> PublicKey;
typedef FixedString<PRIVATE_KEY_LENGTH> PrivateKey;

inline PublicKey getHashedPublicKey(const char* str) {
  PublicKey result;

  blake2sp(result.str, BLAKE2_HASH_LENGTH, str, PUBLIC_KEY_LENGTH, nullptr, 0);

  return result;
}


#endif // __KEYS_HPP__
