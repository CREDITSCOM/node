#include "cscrypto/cscrypto.hpp"

#include <blake2.h>

namespace cscrypto {

void CalculateHash(Hash& hash, const uint8_t* data, size_t data_size) {
  blake2sp(hash.data(), BLAKE2S_OUTBYTES, data, data_size, 0, 0);
}

}  // namespace cscrypto
