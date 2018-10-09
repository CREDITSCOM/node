#ifndef COMMON_HPP
#define COMMON_HPP

#include <string>
#include <array>
#include <vector>

#define STL_STRUCTURES

#ifndef STL_STRUCTURES
#include <lib/system/structures.hpp> 
#endif

constexpr std::size_t HASH_LENGTH = 32;
constexpr std::size_t BLAKE2_HASH_LENGTH = 32;
constexpr std::size_t PUBLIC_KEY_LENGTH = 32;
constexpr std::size_t PRIVATE_KEY_LENGTH = 32;
constexpr std::size_t SIGNATURE_LENGTH = 64;

namespace cs
{
    using Vector = std::string;
    using Matrix = std::string;

    using Byte = uint8_t;

    ///
    /// Dynamic bytes vector
    ///
    using Bytes = std::vector<Byte>;

    ///
    /// Static byte array
    ///
    template<std::size_t size>
    using ByteArray = std::array<Byte, size>;

    // common data structures
#ifdef STL_STRUCTURES
    using PublicKey = ByteArray<PUBLIC_KEY_LENGTH>;
    using Signature = ByteArray<SIGNATURE_LENGTH>;
    using Hash = ByteArray<HASH_LENGTH>;
    using Blacke2Hash = ByteArray<BLAKE2_HASH_LENGTH>;
    using PrivateKey = ByteArray<PRIVATE_KEY_LENGTH>;
#else
    using PublicKey = FixedString<PUBLIC_KEY_LENGTH>;
    using Signature = FixedString<SIGNATURE_LENGTH>;
    using Hash = FixedString<HASH_LENGTH>;
    using Blacke2Hash = FixedString<BLAKE2_HASH_LENGTH>;
    using PrivateKey = FixedString<PRIVATE_KEY_LENGTH>;
#endif
}

#endif // COMMON_HPP

