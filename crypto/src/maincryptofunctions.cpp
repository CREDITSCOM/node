#include "cscrypto/maincryptofunctions.hpp"

#include <cassert>

#include <blake2.h>

namespace cscrypto {

void CalculateHash(Hash& hash, const Byte* data, size_t data_size,
                   const Byte* key, size_t key_size) {
  assert(data != nullptr);
  blake2s(hash.data(), BLAKE2S_OUTBYTES, data, data_size, key, key_size);
}

bool CryptoInit() {
  return sodium_init() >= 0;
}

void GenerateKeyPair(PublicKey& public_key, PrivateKey& private_key) {
  private_key = PrivateKey::generateWithPair(public_key);
}

bool ValidateKeyPair(const PublicKey& public_key, const PrivateKey& private_key) {
  return public_key == GetMatchingPublic(private_key);
}

PublicKey GetMatchingPublic(const PrivateKey& private_key) {
  PublicKey result;
  auto pkg = private_key.access();
  crypto_sign_ed25519_sk_to_pk(result.data(), pkg.data());
  return result;
}

void GenerateSignature(Signature& signature, const PrivateKey& private_key,
                       const Byte* data, size_t data_size) {
  assert(data != nullptr);
  unsigned long long signature_len;
  auto pkg = private_key.access();
  crypto_sign_ed25519_detached(signature.data(), &signature_len, data, data_size, pkg.data());
}

bool VerifySignature(const Signature& signature, const PublicKey& public_key,
                     const Byte* data, size_t data_size) {
  assert(data != nullptr);
  return !crypto_sign_ed25519_verify_detached(signature.data(), data, data_size, public_key.data());
}

bool VerifySignature(const Byte* signature, const Byte* public_key,
                     const Byte* data, size_t data_size) {
  assert(signature != nullptr && public_key != nullptr && data != nullptr);
  return !crypto_sign_ed25519_verify_detached(signature, data, data_size, public_key);
}

void FillBufWithRandomBytes(void* buf, size_t buf_size) {
  assert(buf != nullptr);
  randombytes_buf(buf, buf_size);
}

void FillWithZeros(void* buf, size_t buf_size) {
  assert(buf != nullptr);
  sodium_memzero(buf, buf_size);
}

} // namespace cscrypto
