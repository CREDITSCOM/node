#include "cscrypto/cscrypto.hpp"

#ifdef USE_CRYPTOPP
#include <blake2.h>
#else
#include <blake2.h>
#endif

#include <cstring>

namespace cscrypto {

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

}  // namespace cscrypto
