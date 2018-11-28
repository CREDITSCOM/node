#include "cscrypto/cscrypto.hpp"

#include <blake2.h>
#include <sodium.h>

namespace cscrypto {

void CalculateHash(Hash& hash, const Byte* data, size_t data_size) {
  blake2sp(hash.data(), BLAKE2S_OUTBYTES, data, data_size, 0, 0);
}

void GenerateKeyPair(PublicKey& public_key, PrivateKey& private_key) {
  crypto_sign_keypair(public_key.data(), private_key.data());
}

bool ValidateKeyPair(const PublicKey& public_key, const PrivateKey& private_key) {
  Signature signature;
  uint8_t test_data[] = { 0x01, 0x02, 0xef, 0xfe };
  GenerateSignature(signature, private_key, test_data, sizeof(test_data));
  return VerifySignature(signature, public_key, test_data, sizeof(test_data));
}

void GenerateSignature(Signature& signature, const PrivateKey& private_key,
                       const Byte* data, size_t data_size) {
  unsigned long long signature_len;
  crypto_sign_ed25519_detached(signature.data(), &signature_len, data, data_size, private_key.data());
}

bool VerifySignature(const Signature& signature, const PublicKey& public_key,
                     const Byte* data, size_t data_size) {
  return !crypto_sign_ed25519_verify_detached(signature.data(), data, data_size, public_key.data());
}

bool VerifySignature(const Byte* signature, const Byte* public_key,
  const Byte* data, size_t data_size) {
  return !crypto_sign_ed25519_verify_detached(signature, data, data_size, public_key);
}

}  // namespace cscrypto
