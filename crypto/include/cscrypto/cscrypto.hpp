#ifndef CSCRYPTO_HPP
#define CSCRYPTO_HPP

#include <cinttypes>
#include <cstddef>
#include <array>

namespace cscrypto {

const size_t kHashSize = 32;
const size_t kPublicKeySize = 32;
const size_t kPrivateKeySize = 64;
const size_t kSignatureSize = 64;

using PublicKey = std::array<uint8_t, 32>;
using Hash = std::array<uint8_t, 32>;

Hash blake2s(const uint8_t* data, size_t length);

template <class T>
Hash blake2s(const T& msg) {
  const uint8_t* data = reinterpret_cast<const uint8_t*>(msg.data());
  Hash result = blake2s(data, msg.size());
  return result;
}

}  // namespace cscrypto

#endif  // CSCRYPTO_HPP
