#include "priv_crypto.hpp"

#ifdef CSDB_UNIT_TEST
#include <functional>
#endif

namespace csdb {
namespace priv {

internal::byte_array crypto::calc_hash(const internal::byte_array &buffer) noexcept {
#ifndef CSDB_UNIT_TEST
  cscrypto::Hash result;
  cscrypto::CalculateHash(result, buffer.data(), buffer.size());
  return internal::byte_array(result.begin(), result.end());
#else
  const size_t result = std::hash<std::string>()(std::string(buffer.begin(), buffer.end()));
  return internal::byte_array(reinterpret_cast<const uint8_t *>(&result),
                              reinterpret_cast<const uint8_t *>(&result) + hash_size);
#endif
}

}  // namespace priv
}  // namespace csdb
