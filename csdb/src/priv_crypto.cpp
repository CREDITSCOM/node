#include "priv_crypto.hpp"

#ifdef CSDB_UNIT_TEST
#include <functional>
#endif

namespace csdb {
namespace priv {

cs::Bytes crypto::calc_hash(const cs::Bytes &buffer) noexcept {
#ifndef CSDB_UNIT_TEST
  cscrypto::Hash result;
  cscrypto::CalculateHash(result, buffer.data(), buffer.size());
  return cs::Bytes(result.begin(), result.end());
#else
  const size_t result = std::hash<std::string>()(std::string(buffer.begin(), buffer.end()));
  return internal::byte_array(reinterpret_cast<const uint8_t *>(&result),
                              reinterpret_cast<const uint8_t *>(&result) + hash_size);
#endif
}

}  // namespace priv
}  // namespace csdb
