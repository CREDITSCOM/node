#include "priv_crypto.h"

#ifndef CSDB_UNIT_TEST
#include "cscrypto/cscrypto.h"
#else
#include <functional>
#endif

namespace csdb {
namespace priv {

#ifndef CSDB_UNIT_TEST
const size_t crypto::hash_size = cscrypto::Hash::sizeBytes;
const size_t crypto::public_key_size = cscrypto::PublicKey::sizeBytes;
#else
const size_t crypto::hash_size = sizeof(size_t);
const size_t crypto::public_key_size = 20;
#endif

internal::byte_array crypto::calc_hash(const internal::byte_array &buffer) noexcept
{
#ifndef CSDB_UNIT_TEST
  const cscrypto::Hash result = cscrypto::blake2s(buffer);
  return internal::byte_array(result.bytes.begin(), result.bytes.end());
#else
  const size_t result = std::hash<std::string>()( std::string(buffer.begin(), buffer.end()) );
  return internal::byte_array(reinterpret_cast<const uint8_t*>(&result), reinterpret_cast<const uint8_t*>(&result) + hash_size);
#endif
}

} // namespace priv
} // namespace csdb
