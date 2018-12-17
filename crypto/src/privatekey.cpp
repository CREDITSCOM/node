#include "cscrypto/privatekey.hpp"

namespace cscrypto {

PrivateKey::PrivateKey(): mem_(nullptr), ctr_(new uint32_t(1)) {}

void PrivateKey::clear() {
  if (!(--(*ctr_))) {
    sodium_free(mem_);
    free(ctr_);
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

} // namespace cscrypto
