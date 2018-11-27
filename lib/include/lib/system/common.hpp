#ifndef COMMON_HPP
#define COMMON_HPP

#include <array>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>

#include <boost/smart_ptr/detail/spinlock.hpp>

#include <cscrypto/cscrypto.hpp>

constexpr std::size_t HASH_LENGTH = cscrypto::kHashSize;
constexpr std::size_t PUBLIC_KEY_LENGTH = cscrypto::kPublicKeySize;
constexpr std::size_t PRIVATE_KEY_LENGTH = cscrypto::kPrivateKeySize;
constexpr std::size_t SIGNATURE_LENGTH = cscrypto::kSignatureSize;

namespace cs {
using Vector = std::string;
using Matrix = std::string;

using RoundNumber = uint32_t;

using Byte = uint8_t;

// dynamic vector of bytes
using Bytes = std::vector<Byte>;

// static byte array
template <std::size_t size>
using ByteArray = std::array<Byte, size>;

// common data structures
using PublicKey = cscrypto::PublicKey;
using Signature = cscrypto::Signature;
using Hash = cscrypto::Hash;
using PrivateKey = cscrypto::PrivateKey;

// sync types
using SharedMutex = std::shared_mutex;
using SpinLock = boost::detail::spinlock;

// RAII locks
using Lock = std::lock_guard<cs::SharedMutex>;
using SharedLock = std::shared_lock<cs::SharedMutex>;
using SpinGuard = std::lock_guard<SpinLock>;
}  // namespace cs

#endif  // COMMON_HPP
