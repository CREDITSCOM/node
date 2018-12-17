#include "cscrypto/maincryptofunctions.hpp"

#include <cassert>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>

#include <blake2.h>

namespace cscrypto {

static auto getKeyFromPassword(const char* passwd) {
  MemGuard<Byte, crypto_aead_chacha20poly1305_KEYBYTES> key;
  const static std::array<Byte, crypto_pwhash_SALTBYTES> salt = { 73, 68, 73, 32, 78, 65, 72, 85, 89, 32, 66, 76, 89, 65, 84, 39 };

  if (crypto_pwhash(key.data(),
                    key.size(),
                    passwd,
                    std::strlen(passwd),
                    salt.data(),
                    crypto_pwhash_OPSLIMIT_SENSITIVE,
                    crypto_pwhash_MEMLIMIT_SENSITIVE,
                    crypto_pwhash_ALG_DEFAULT) != 0) {
    key.clear();
  }


  return key;
}

PrivateKey PrivateKey::readFromBytes(const std::vector<Byte>& bytes) {
  if (bytes.size() != kPrivateKeySize) return PrivateKey();

  PrivateKey result;
  result.mem_ = sodium_malloc(kPrivateKeySize);
  memcpy(result.mem_, bytes.data(), kPrivateKeySize);
  sodium_mprotect_noaccess(result.mem_);
  return result;
}

PrivateKey PrivateKey::readFromEncrypted(const std::vector<Byte>& bytes, const char* passwd) {
  if (bytes.size() < crypto_aead_chacha20poly1305_NPUBBYTES)
    return PrivateKey();

  MemGuard<Byte, crypto_aead_chacha20poly1305_NPUBBYTES> nonce;
  memcpy(nonce.data(), bytes.data(), crypto_aead_chacha20poly1305_NPUBBYTES);

  // Hash the password
  auto key = getKeyFromPassword(passwd);

  PrivateKey result;
  result.mem_ = sodium_malloc(kPrivateKeySize);

  long long unsigned decrypted_len = 0;
  if (crypto_aead_chacha20poly1305_decrypt(static_cast<Byte*>(result.mem_),
                                           &decrypted_len,
                                           NULL,
                                           bytes.data() + crypto_aead_chacha20poly1305_NPUBBYTES,
                                           bytes.size() - crypto_aead_chacha20poly1305_NPUBBYTES,
                                           NULL, 0,
                                           nonce.data(),
                                           key.data()) != 0 ||
      decrypted_len != kPrivateKeySize) {
    return PrivateKey();
  }

  sodium_mprotect_noaccess(result.mem_);
  return result;
}

std::vector<Byte> PrivateKey::getEncrypted(const char* passwd) const {
  std::vector<Byte> result;

  auto key = getKeyFromPassword(passwd);
  auto pk = access();

  Byte cText[kPrivateKeySize + crypto_aead_chacha20poly1305_ABYTES];
  long long unsigned cSize = 0;

  MemGuard<Byte, crypto_aead_chacha20poly1305_NPUBBYTES> nonce;
  randombytes_buf(nonce.data(), nonce.size());

  if (crypto_aead_chacha20poly1305_encrypt(cText, &cSize,
                                           pk.data(), pk.size(),
                                           NULL, 0,
                                           NULL, nonce.data(), key.data()) != 0) {
    return result;
  }

  result.resize(crypto_aead_chacha20poly1305_NPUBBYTES + cSize);

  memcpy(result.data(),
         nonce.data(),
         crypto_aead_chacha20poly1305_NPUBBYTES);

  memcpy(result.data() + crypto_aead_chacha20poly1305_NPUBBYTES,
         cText,
         cSize);

  return result;
}

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
