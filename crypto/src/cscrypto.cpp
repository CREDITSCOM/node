#include "cscrypto/cscrypto.hpp"

#ifdef USE_CRYPTOPP
#include <blake2.h>
#else
#include <blake2.h>
#endif

#include <cstring>

#include <mutex>

namespace cscrypto {
namespace detail {
struct Context {
  byte seed[32] = {};

  std::mutex mutex;

  Context() = default;
  ~Context() = default;

  // Disallow copy and assign
  Context(const Context&) = delete;
  Context& operator=(const Context&) = delete;
  Context(Context&&) = delete;
  Context& operator=(Context&&) = delete;
};

using ScopedLock = std::lock_guard<std::mutex>;
}  // namespace detail

using namespace detail;

Hash blake2s(const byte* data, size_t length) {
#ifdef USE_CRYPTOPP

  using namespace CryptoPP;

  static_assert(Hash256Bit::sizeBytes == BLAKE2sp::DIGESTSIZE, "Hash256Bit size doesn't match Blake2s digest size");

  Hash256Bit result;

  BLAKE2sp blake2s;
  blake2s.Update(data, length);
  blake2s.Final(result.data());

  return result;

#else

  static_assert(Hash::sizeBytes == BLAKE2S_OUTBYTES, "Hash256Bit size doesn't match Blake2s digest size");

  Hash result;

  blake2sp(result.data(), BLAKE2S_OUTBYTES, data, length, nullptr, 0);

#endif  // #ifdef USE_CRYPTOPP

  return result;
}

/// Convert the given value into h160 (160-bit unsigned integer) using the right 20 bytes.
using Hash160Bit = FixedArray<160>;
inline Hash160Bit right160(const Hash& h256) {
  Hash160Bit ret;
  memcpy(ret.data(), h256.data() + 12, 20);
  return ret;
}

Address toAddress(const PublicKey& publicKey) {
  // Ethereum does right160(sha3(public))
  return Address{right160(blake2s(publicKey))};
}
}  // namespace cscrypto
