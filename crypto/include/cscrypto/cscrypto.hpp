#ifndef CSCRYPTO_HPP
#define CSCRYPTO_HPP

#include <stdint.h>
#include <array>
#include <string>
#include <vector>

namespace cscrypto {
using byte = uint8_t;
using Bytes = std::vector<byte>;

template <uint16_t BITS>
struct FixedArray {
  static const uint16_t sizeBits = BITS;
  static const uint8_t sizeBytes = sizeBits / 8;

  typedef uint8_t byte;

  typedef std::array<byte, sizeBytes> Data;
  Data bytes = {};

  FixedArray() = default;
  explicit FixedArray(const Bytes& source) {
    size_t len = std::min((size_t)sizeBytes, source.size());

    bytes.fill(0);

    for (size_t i = 0; i < len; ++i)
      bytes[i] = source[i];
  }

  explicit FixedArray(const byte* source) {
    for (size_t i = 0; i < sizeBytes; ++i)
      bytes[i] = source[i];
  }

  std::string toString() const {
    std::string digest = bytesToHexString(bytes);
    return digest;
  }

  typename Data::pointer data() {
    return bytes.data();
  }
  typename Data::const_pointer data() const {
    return bytes.data();
  }

  size_t size() const {
    return sizeBytes;
  }
};

using PublicKey = FixedArray<256>;
using Hash = FixedArray<256>;

Hash blake2s(const byte* data, size_t length);

template <class T>
Hash blake2s(const T& msg) {
  const byte* data = reinterpret_cast<const byte*>(msg.data());
  Hash result = blake2s(data, msg.size());
  return result;
}

}  // namespace cscrypto

#endif  // CSCRYPTO_HPP
