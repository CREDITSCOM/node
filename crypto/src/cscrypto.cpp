#include "cscrypto/cscrypto.hpp"

#include <blake2.h>

namespace cscrypto {

Hash blake2s(const uint8_t* data, size_t length) {
  Hash result;
  blake2sp(result.data(), BLAKE2S_OUTBYTES, data, length, nullptr, 0);
  return result;
}

}  // namespace cscrypto
