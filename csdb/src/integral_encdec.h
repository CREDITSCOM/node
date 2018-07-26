/**
  * @file encdec.h
  * @author Evgeny V. Zalivochkin
  *
  * Файл содержит функции для компактного кодирования целочисленных типов. Максимальный
  * размер кодируемого типа - 8 байт.
  */

#pragma once
#ifndef _CREDITS_CSDB_PRIVATE_INTEGRAL_ENCDEC_H_INCLUDED_
#define _CREDITS_CSDB_PRIVATE_INTEGRAL_ENCDEC_H_INCLUDED_

#include <cinttypes>
#include <type_traits>

namespace csdb {
namespace priv {
enum {
  MAX_INTEGRAL_ENCODED_SIZE = sizeof(uint64_t) + 1,
};

/**
 * @brief Компактное кодирование переменной целочисленного типа.
 * @param[out]  buf   Буфер, в которые поместить закодированное значени. Буфер должен
 *                    иметь размер не менее \ref MAX_INTEGRAL_ENCODED_SIZE.
 * @param[in]   value Значение, которое закодировать. Допустимы целые типы размером не
 *                    больше, чем sizeof(uint64) байт.
 * @return  Количество байт, записанное в буфер.
 */
template<typename T>
typename std::enable_if<(std::is_integral<T>::value || std::is_enum<T>::value)
                        && (sizeof(T) <= sizeof(uint64_t)), std::size_t>::type
encode(void *buf, T value)
{
  return encode<uint64_t>(buf, static_cast<uint64_t>(value));
}

/**
 * @brief Декодирование целочисленного типа.
 * @param[in]   buf   Буфер, в котором лежат данный для декодирования.
 * @param[in]   size  Размер данных в буфере.
 * @param[out]  value Переменная целочисленного типа, куда поместить резальтат
 *                    декодирования. Допустимы целые типы размером не больше, чем
 *                    sizeof(uint64) байт.
 * @return  Количество байт, прочитынных из \ref buf. 0, если закодированные данные
 *          содержат ошибку или данных недостаточно для полного декодирования.
 */
template<typename T>
typename std::enable_if<(std::is_integral<T>::value || std::is_enum<T>::value)
                         && (sizeof(T) <= sizeof(uint64_t)), std::size_t>::type
decode(const void *buf, std::size_t size, T& value)
{
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

} // namespace priv
} // namespace csdb

#endif // _CREDITS_CSDB_PRIVATE_INTEGRAL_ENCDEC_H_INCLUDED_
