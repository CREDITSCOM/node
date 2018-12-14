#ifndef CSCRYPTO_HPP
#define CSCRYPTO_HPP

#include <cinttypes>
#include <cstddef>
#include <array>
#include <vector>
#include <string_view>

#include <sodium.h>

namespace cscrypto {

const size_t kHashSize = 32;
const size_t kPublicKeySize = 32;
const size_t kPrivateKeySize = 64;
const size_t kSignatureSize = 64;

using Byte = uint8_t;

template <std::size_t size>
using ByteArray = std::array<cscrypto::Byte, size>;
using Bytes = std::vector<cscrypto::Byte>;
using BytesView = std::basic_string_view<cscrypto::Byte>;

using Hash = cscrypto::ByteArray<kHashSize>;
using PublicKey = cscrypto::ByteArray<kPublicKeySize>;
using Signature = cscrypto::ByteArray<kSignatureSize>;

template <typename T, std::size_t Size>
class MemGuard {
public:
  MemGuard();
  ~MemGuard() { clear(); }

  MemGuard(MemGuard&&);
  MemGuard& operator=(MemGuard&&);

  MemGuard(const MemGuard&) = delete;
  MemGuard& operator=(const MemGuard&);

  T* data() { return mem_; }
  const T* data() const { return mem_; }

  auto size() const { return Size; }

  void clear();
  operator bool() const { return mem_; }

private:
  T* mem_;
};

template <typename T, std::size_t Size>
class MemAccessGuard {
public:
  MemAccessGuard(T* mem);
  ~MemAccessGuard() { clear(); }

  MemAccessGuard(MemAccessGuard&&);
  MemAccessGuard& operator=(MemAccessGuard&&);

  MemAccessGuard(const MemAccessGuard&) = delete;
  MemAccessGuard& operator=(const MemAccessGuard&) = delete;

  const T* data() const { return mem_; }
  auto size() const { return Size; }

private:
  void clear();
  T* mem_;
};

using PrivateKeyGuard = MemAccessGuard<cscrypto::Byte, kPrivateKeySize>;

class PrivateKey {
public:
  PrivateKey();
  ~PrivateKey();

  PrivateKey(const PrivateKey&);
  PrivateKey(PrivateKey&&);
  PrivateKey& operator=(const PrivateKey&);
  PrivateKey& operator=(PrivateKey&&);

  PrivateKeyGuard access() const;
  operator bool() const { return mem_; }

  static PrivateKey readFromBytes(const std::vector<Byte>&);
  static PrivateKey readFromEncrypted(const std::vector<Byte>&, const char* passwd);

  std::vector<Byte> getEncrypted(const char* passwd) const;
  static PrivateKey generateWithPair(PublicKey&);

private:
  void* mem_;
  uint32_t* ctr_;
};

bool CryptoInit();

void CalculateHash(Hash& hash, const Byte* data, size_t data_size);

void GenerateKeyPair(PublicKey& public_key, PrivateKey& private_key);

bool ValidateKeyPair(const PublicKey& public_key, const PrivateKey& private_key);

PublicKey GetMatchingPublic(const PrivateKey& private_key);

void GenerateSignature(Signature& signature, const PrivateKey& private_key,
                       const Byte* data, size_t data_size);

bool VerifySignature(const Signature& signature, const PublicKey& public_key,
                     const Byte* data, size_t data_size);

bool VerifySignature(const Byte* signature, const Byte* public_key,
  const Byte* data, size_t data_size);

void FillBufWithRandomBytes(void* buf, size_t buf_size);
void FillWithZeros(void* buf, size_t buf_size);

template <typename T, std::size_t Size>
MemGuard<T, Size>::MemGuard() {
  mem_ = static_cast<T*>(sodium_malloc(Size * sizeof(T)));
}

template <typename T, std::size_t Size>
void MemGuard<T, Size>::clear() {
  sodium_free(reinterpret_cast<void*>(mem_));
  mem_ = nullptr;
}

template <typename T, std::size_t Size>
MemGuard<T, Size>::MemGuard(MemGuard&& rhs) {
  if (rhs.mem_ != mem_) clear();

  mem_ = rhs.mem_;
  rhs.mem_ = nullptr;
}

template <typename T, std::size_t Size>
MemGuard<T, Size>& MemGuard<T, Size>::operator=(MemGuard&& rhs) {
  if (rhs.mem_ != mem_) clear();

  mem_ = rhs.mem_;
  rhs.mem_ = nullptr;

  return *this;
}

template <typename T, std::size_t Size>
MemAccessGuard<T, Size>::MemAccessGuard(T* mem): mem_(mem) {
  sodium_mprotect_readonly(mem_);
}

template <typename T, std::size_t Size>
void MemAccessGuard<T, Size>::clear() {
  sodium_mprotect_noaccess(mem_);
}

template <typename T, std::size_t Size>
MemAccessGuard<T, Size>::MemAccessGuard(MemAccessGuard&& rhs) {
  if (rhs.mem_ != mem_) clear();

  mem_ = rhs.mem_;
  rhs.mem_ = nullptr;
}

template <typename T, std::size_t Size>
MemAccessGuard<T, Size>& MemAccessGuard<T, Size>::operator=(MemAccessGuard&& rhs) {
  if (rhs.mem_ != mem_) clear();

  mem_ = rhs.mem_;
  rhs.mem_ = nullptr;

  return *this;
}

}  // namespace cscrypto

#endif  // CSCRYPTO_HPP
