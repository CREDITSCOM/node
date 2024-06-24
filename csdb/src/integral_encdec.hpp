/**
 * @file encdec.h
 * @author Evgeny V. Zalivochkin
 *
 * this file contains methods for compact integer types coding. Maximum 
 * size of coding type is 8 bytes.
 */

#pragma once
#ifndef _CREDITS_CSDB_PRIVATE_INTEGRAL_ENCDEC_H_INCLUDED_
#define _CREDITS_CSDB_PRIVATE_INTEGRAL_ENCDEC_H_INCLUDED_

#include <cinttypes>
#include <type_traits>

namespace csdb {
namespace priv {
enum
{
    MAX_INTEGRAL_ENCODED_SIZE = sizeof(uint64_t) + 1,
};

/**
 * @brief Compact coding of integer type variable.
 * @param[out]  buf   Buffer used to place coding value. Buffer should have size 
 * not less then \ref MAX_INTEGRAL_ENCODED_SIZE.
 * @param[in]   value Value to be coded. Integer types with sizes not larger than 
 *                    sizeof(uint64) bytes are acceptable.
 * @return  Amount of bytes, written in buffer.
 */
template <typename T>
typename std::enable_if<(std::is_integral<T>::value || std::is_enum<T>::value) && (sizeof(T) <= sizeof(uint64_t)), std::size_t>::type encode(void *buf, T value) {
    return encode<uint64_t>(buf, static_cast<uint64_t>(value));
}

/**
 * @brief Decoding of integer type value
 * @param[in]   buf   Buffer, contains decoding data.
 * @param[in]   size  Buffer data size.
 * @param[out]  value Integer type variable where the decoding result should be saved.
 *                    Acceptable integer types with sizes not larger then sizeof(uint64) bytes.
 * @return  Number of bytes, read from \ref buf. 0, if encoded data is erroneous or 
 *          there is not enough data for full decoding.
 */
template <typename T>
typename std::enable_if<(std::is_integral<T>::value || std::is_enum<T>::value) && (sizeof(T) <= sizeof(uint64_t)), std::size_t>::type decode(const void *buf, std::size_t size,
                                                                                                                                             T &value) {
    uint64_t v;
    std::size_t res = decode<uint64_t>(buf, size, v);
    if (0 != res) {
        value = static_cast<T>(v);
    }
    return res;
}

template<>
std::size_t encode(void *buf, bool value);

template<>
std::size_t encode(void *buf, uint64_t value);

template<>
std::size_t decode(const void *buf, std::size_t size, bool& value);

template<>
std::size_t decode(const void *buf, std::size_t size, uint64_t& value);

}  // namespace priv
}  // namespace csdb

#endif // _CREDITS_CSDB_PRIVATE_INTEGRAL_ENCDEC_H_INCLUDED_
