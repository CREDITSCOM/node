#include "cscrypto/cscrypto.hpp"

#include <cassert>

#include <blake2.h>
#include <sodium.h>

namespace cscrypto {

void CalculateHash(Hash& hash, const uint8_t* data, size_t data_size) {
  assert(data != nullptr);
  blake2sp(hash.data(), BLAKE2S_OUTBYTES, data, data_size, 0, 0);
}

void GenerateKeyPair(PublicKey& public_key, PrivateKey& private_key) {
  if (sodium_init() < 0) return;
  crypto_sign_keypair(public_key.data(), private_key.data());
}

bool ValidateKeyPair(const PublicKey& public_key, const PrivateKey& private_key) {
  if (sodium_init() < 0) return false;
  Signature signature;
  uint8_t test_data[] = { 0x01, 0x02, 0xef, 0xfe };
  GenerateSignature(signature, private_key, test_data, sizeof(test_data));
  return VerifySignature(signature, public_key, test_data, sizeof(test_data));
}

void GenerateSignature(Signature& signature, const PrivateKey& private_key,
                       const uint8_t* data, size_t data_size) {
  assert(data != nullptr);
  if (sodium_init() < 0) return;
  unsigned long long signature_len;
  crypto_sign_ed25519_detached(signature.data(), &signature_len, data, data_size, private_key.data());
}

bool VerifySignature(const Signature& signature, const PublicKey& public_key,
                     const uint8_t* data, size_t data_size) {
  assert(data != nullptr);
  if (sodium_init() < 0) return false;
  return !crypto_sign_ed25519_verify_detached(signature.data(), data, data_size, public_key.data());
}

bool VerifySignature(const uint8_t* signature, const uint8_t* public_key,
  const uint8_t* data, size_t data_size) {
  assert(signature != nullptr && public_key != nullptr && data != nullptr);
  if (sodium_init() < 0) return false;
  return !crypto_sign_ed25519_verify_detached(signature, data, data_size, public_key);
}

void FillBufWithRandomBytes(void* buf, size_t buf_size) {
  assert(buf != nullptr);
  if (sodium_init() < 0) return;
  randombytes_buf(buf, buf_size);
}

}  // namespace cscrypto
