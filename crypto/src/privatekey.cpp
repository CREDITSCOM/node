#include <cstring>
#include <memory.h>

#include "cscrypto/privatekey.hpp"

namespace cscrypto {

PrivateKey::PrivateKey(): mem_(nullptr), ctr_(new uint32_t(1)) {}

void PrivateKey::clear() {
  if (!(--(*ctr_))) {
    sodium_free(mem_);
    delete ctr_;
  }
}

PrivateKey::PrivateKey(const PrivateKey& rhs): mem_(rhs.mem_), ctr_(rhs.ctr_) {
  ++(*ctr_);
}

PrivateKey::PrivateKey(PrivateKey&& rhs): PrivateKey(rhs) {}

PrivateKey& PrivateKey::operator=(const PrivateKey& rhs) {
  if (rhs.mem_ != mem_) clear();

  mem_ = rhs.mem_;
  ctr_ = rhs.ctr_;
  ++(*ctr_);

  return *this;
}

PrivateKey& PrivateKey::operator=(PrivateKey&& rhs) {
  return (*this = rhs);
}

PrivateKeyGuard PrivateKey::access() const {
  return PrivateKeyGuard(static_cast<Byte*>(mem_));
}

PrivateKey PrivateKey::generateWithPair(PublicKey& public_key) {
  PrivateKey result;
  result.mem_ = sodium_malloc(kPrivateKeySize);
  crypto_sign_keypair(public_key.data(), static_cast<Byte*>(result.mem_));
  sodium_mprotect_noaccess(result.mem_);
  return result;
}

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

PrivateKey PrivateKey::readFromBytes(const Bytes& bytes) {
  if (bytes.size() != kPrivateKeySize) return PrivateKey();

  PrivateKey result;
  result.mem_ = sodium_malloc(kPrivateKeySize);
  memcpy(result.mem_, bytes.data(), kPrivateKeySize);
  sodium_mprotect_noaccess(result.mem_);
  return result;
}

PrivateKey PrivateKey::readFromEncrypted(const Bytes& bytes, const char* passwd) {
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

Bytes PrivateKey::getEncrypted(const char* passwd) const {
  Bytes result;

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

} // namespace cscrypto
