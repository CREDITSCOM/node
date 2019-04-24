#include "integral_encdec.hpp"

#include <cstring>
#ifdef _MSC_VER
#include <intrin.h>
#endif

#include "csdb/internal/endian.hpp"

namespace csdb {
namespace priv {

template <>
std::size_t encode(void *buf, bool value) {
    *(static_cast<uint8_t *>(buf)) = value ? '\x01' : '\x00';
    return sizeof(uint8_t);
}

template <>
std::size_t encode(void *buf, uint64_t value) {
    value = ::csdb::internal::to_little_endian(value);

    uint64_t copy = (value & 0x8000000000000000) != 0u ? ~value : value;
#ifdef _MSC_VER
    unsigned long bits;
    _BitScanReverse64(&bits, copy);
    bits += 2;
#else
    uint64_t bits = (__builtin_clzl(copy) ^ 63) + 2;
#endif
    if (bits < 7) {
        bits = 7;
    }

    uint8_t bytes = 8;
    uint8_t first = (uint8_t)'\xFF';
    if (57 > bits) {
        bytes = static_cast<uint8_t>((bits - 1) / 7);
        first = static_cast<char>((value << (bytes + 1u)) | ((1u << bytes) - 1u));
        value >>= (7 - bytes);
    }
    auto buffer = static_cast<uint8_t *>(buf);
    *(buffer++) = first;
    if (0 < bytes) {
        memcpy(buffer, &value, bytes);
    }
    return bytes + sizeof(uint8_t);
}

template <>
std::size_t decode(const void *buf, std::size_t size, bool &value) {
    if (sizeof(uint8_t) > size) {
        return 0;
    }
    value = ('\0' != (*static_cast<const uint8_t *>(buf)));
    return sizeof(uint8_t);
}

template <>
std::size_t decode(const void *buf, std::size_t size, uint64_t &value) {
    if (sizeof(uint8_t) > size) {
        return 0;
    }
    auto d = static_cast<const char *>(buf);
    auto first = static_cast<uint8_t>(*d);
    uint8_t bytes = 0;
    for (uint8_t mask = first; 0 != (mask & 0x1u); ++bytes, mask >>= 1) {
    }
    if ((bytes + 1u) > size) {
        return 0;
    }

    uint64_t val = 0;
    if (bytes > 0) {
        memcpy(&val, d + 1, bytes);
    }
    if (8 > bytes) {
        val <<= (7 - bytes);
        val |= (first >> (bytes + 1u));
        if (0u != (d[bytes] & 0x80)) {
            val |= static_cast<uint64_t>(-1) << ((bytes + 1u) * 7);
        }
    }
    value = ::csdb::internal::from_little_endian(val);
    return bytes + 1;
}

}  // namespace priv
}  // namespace csdb
