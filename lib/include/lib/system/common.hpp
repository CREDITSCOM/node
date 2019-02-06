#ifndef COMMON_HPP
#define COMMON_HPP

#include <mutex>
#include <shared_mutex>
#include <string>

#include <boost/smart_ptr/detail/spinlock.hpp>

#include <cscrypto/cscrypto.hpp>

constexpr std::size_t HASH_LENGTH = cscrypto::kHashSize;
constexpr std::size_t PUBLIC_KEY_LENGTH = cscrypto::kPublicKeySize;
constexpr std::size_t PRIVATE_KEY_LENGTH = cscrypto::kPrivateKeySize;
constexpr std::size_t SIGNATURE_LENGTH = cscrypto::kSignatureSize;

namespace cs {
// key node type
using RoundNumber = uint64_t;
using Sequence = RoundNumber;

using Byte = cscrypto::Byte;

// dynamic vector of bytes
using Bytes = cscrypto::Bytes;
using BytesView = cscrypto::BytesView;

// static byte array
template <std::size_t size>
using ByteArray = cscrypto::ByteArray<size>;

// common data structures
using PublicKey = cscrypto::PublicKey;
using Signature = cscrypto::Signature;
using Hash = cscrypto::Hash;
using PrivateKey = cscrypto::PrivateKey;

// sync types
using SharedMutex = std::shared_mutex;
using SpinLock = boost::detail::spinlock;

// RAII locks
// TODO: simple lock_guard and shared_lock using next?
template<typename T>
class Lock : public std::lock_guard<T> {
public:
  explicit inline Lock(T& lockable) noexcept : std::lock_guard<T>(lockable) {}
};

template<typename T>
class SharedLock : public std::shared_lock<T> {
public:
  explicit inline SharedLock(T& lockable) noexcept : std::shared_lock<T>(lockable) {}
};

// aliasing, C++ 17 scoped lock, C++ 17 constructor template parameters deduction
template<typename... T>
class ScopedLock {
public:
  explicit inline ScopedLock(T&... locables) noexcept : lock_(locables...) {}
private:
  std::scoped_lock<T...> lock_;
};
}  // namespace cs
#endif  // COMMON_HPP
