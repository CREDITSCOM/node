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

using Hash = std::array<uint8_t, kHashSize>;
using PublicKey = std::array<uint8_t, kPublicKeySize>;
using PrivateKey = std::array<uint8_t, kPrivateKeySize>;
using Signature = std::array<uint8_t, kSignatureSize>;

void CalculateHash(Hash& hash, const uint8_t* data, size_t data_size);

void GenerateKeyPair(PublicKey& public_key, PrivateKey& private_key);

bool ValidateKeyPair(const PublicKey& public_key, const PrivateKey& private_key);

void GenerateSignature(Signature& signature, const PrivateKey& private_key,
                       const uint8_t* data, size_t data_size);

bool VerifySignature(const Signature& signature, const PublicKey& public_key,
                     const uint8_t* data, size_t data_size);

}  // namespace cscrypto

#endif  // CSCRYPTO_HPP
