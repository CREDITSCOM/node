#ifndef CSCRYPTO_MEMORY_PROTECTION_HPP
#define CSCRYPTO_MEMORY_PROTECTION_HPP

#include <cstddef>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4324) // 'crypto_generichash_blake2b_state': structure was padded due to alignment specifier
#endif
#include <sodium.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace cscrypto {

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

} // namespace cscrypto
#endif // CSCRYPTO_MEMORY_PROTECTION_HPP
