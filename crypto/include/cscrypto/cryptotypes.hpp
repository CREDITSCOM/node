#ifndef CSCRYPTO_CRYPTOTYPES_HPP
#define CSCRYPTO_CRYPTOTYPES_HPP

#include "cscrypto/cryptoconstants.hpp"

#include <array>
#include <cinttypes>
#include <string_view>
#include <vector>

namespace cscrypto {

using Byte = uint8_t;

template <std::size_t size>
using ByteArray = std::array<cscrypto::Byte, size>;
using Bytes = std::vector<cscrypto::Byte>;
using BytesView = std::basic_string_view<cscrypto::Byte>;

using Hash = cscrypto::ByteArray<kHashSize>;
using PublicKey = cscrypto::ByteArray<kPublicKeySize>;
using Signature = cscrypto::ByteArray<kSignatureSize>;

}  // namespace cscrypto
#endif  // CSCRYPTO_CRYPTOTYPES_HPP
