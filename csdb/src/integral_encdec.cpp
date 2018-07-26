#include "integral_encdec.h"

#include <cstring>

#include "csdb/internal/endian.h"

namespace csdb {
namespace priv {

template<>
std::size_t encode(void *buf, bool value)
{
  *(static_cast<uint8_t*>(buf)) = value ? '\x01' : '\x00';
  return sizeof(uint8_t);
}

template<>
std::size_t encode(void *buf, uint64_t value)
{
  value = ::csdb::internal::to_little_endian(value);
  uint8_t bits = (sizeof(uint64_t) * 8);
  uint64_t mask = (static_cast<uint64_t>(1) << (bits - 1));
  bool new_bit = (0 != (mask & value)), bit;
  do {
    bit = new_bit;
    mask >>= 1;
    new_bit = (0 != (mask & value));
  } while ((bit == new_bit) && (7 < (--bits)));

  uint8_t bytes = 8;
  uint8_t first = '\xFF';
  if (57 > bits) {
    bytes = ((bits - 1) / 7);
    first = static_cast<char>((value << (bytes + 1)) | ((1 << bytes) - 1));
    value >>= (7 - bytes);
  }
  uint8_t *buffer = static_cast<uint8_t*>(buf);
  *(buffer++) = first;
  if (0 < bytes) {
    memcpy(buffer, &value, bytes);
  }
  return bytes + sizeof(uint8_t);
}

template<>
std::size_t decode(const void *buf, std::size_t size, bool& value)
{
  if (sizeof(uint8_t) > size) {
    return 0;
  }
  value = ('\0' != (*static_cast<const uint8_t*>(buf)));
  return sizeof(uint8_t);
}

template<>
std::size_t decode(const void *buf, std::size_t size, uint64_t& value)
{
  if (sizeof(uint8_t) > size) {
    return 0;
  }
  const char* d = static_cast<const char*>(buf);
  uint8_t first = static_cast<uint8_t>(*d);
  uint8_t bytes = 0;
  for (uint8_t mask = first; 0 != (mask & 0x1); ++bytes, mask >>= 1) {}
  if ((bytes + 1) > size) {
    return 0;
  }

  uint64_t val = 0;
  if (bytes > 0) {
    memcpy(&val, d + 1, bytes);
  }
  if (8 > bytes) {
    val <<= (7 - bytes);
    val |= (first >> (bytes + 1));
    if (0 != (d[bytes] & 0x80)) {
      val |= static_cast<uint64_t>(-1) << ((bytes + 1) * 7);
    }
  }
  value = ::csdb::internal::from_little_endian(val);
  return bytes + 1;
}

} // namespace priv
} // namespace csdb
