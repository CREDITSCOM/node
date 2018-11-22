/**
 * @file endian.h
 * @author Evgeny V. Zalivochkin
 */

#pragma once
#ifndef _CREDITS_CSDB_INTERNAL_ENDIAN_H_INCLUDED_
#define _CREDITS_CSDB_INTERNAL_ENDIAN_H_INCLUDED_

#include <cinttypes>
#include <cstddef>
#include <type_traits>

namespace csdb {
namespace internal {

template <typename T, size_t size>
struct _reverse_byte_order_helper {
  static inline T constexpr value(T v);
};

template <typename T>
T inline constexpr reverse_byte_order(T value) {
  return _reverse_byte_order_helper<T, sizeof(T)>::value(value);
}

#if defined(CSDB_PLATFORM_IS_LITTLE_ENDIAN)

template <typename T>
typename ::std::enable_if<::std::is_integral<T>::value, T>::type inline constexpr to_little_endian(T value) {
  return value;
}

template <typename T>
typename ::std::enable_if<::std::is_integral<T>::value, T>::type inline constexpr from_little_endian(T value) {
  return value;
}

template <typename T>
typename ::std::enable_if<::std::is_integral<T>::value, T>::type inline constexpr to_big_endian(T value) {
  return reverse_byte_order(value);
}

template <typename T>
typename ::std::enable_if<::std::is_integral<T>::value, T>::type inline constexpr from_big_endian(T value) {
  return reverse_byte_order(value);
}

#elif defined(CSDB_PLATFORM_IS_BIG_ENDIAN)

template <typename T>
typename ::std::enable_if<::std::is_integral<T>::value, T>::type inline constexpr to_little_endian(T value) {
  return reverse_byte_order(value);
}

template <typename T>
typename ::std::enable_if<::std::is_integral<T>::value, T>::type inline constexpr from_little_endian(T value) {
  return reverse_byte_order(value);
}

template <typename T>
typename ::std::enable_if<::std::is_integral<T>::value, T>::type inline constexpr to_big_endian(T value) {
  return value;
}

template <typename T>
typename ::std::enable_if<::std::is_integral<T>::value, T>::type inline constexpr from_big_endian(T value) {
  return value;
}

#else
#error Configuration error - platform endian is not defined!
#endif

template <typename T, size_t size>
inline T constexpr _reverse_byte_order_helper<T, size>::value(T v) {
  static_assert((1 < size) && (sizeof(T) >= size), "Invalid usage.");
  return (v & 0xFF) << ((size - 1) * 8) | _reverse_byte_order_helper<T, size - 1>::value(v >> 8);
}

template <typename T>
struct _reverse_byte_order_helper<T, 1> {
  static inline T constexpr value(T v) {
    return v;
  }
};

}  // namespace internal
}  // namespace csdb

#endif  // _CREDITS_CSDB_INTERNAL_ENDIAN_H_INCLUDED_
