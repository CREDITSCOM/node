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

using PublicKey = std::array<uint8_t, kPublicKeySize>;
using Hash = std::array<uint8_t, kHashSize>;

void CalculateHash(Hash& hash, const uint8_t* data, size_t data_size);

}  // namespace cscrypto

#endif  // CSCRYPTO_HPP
