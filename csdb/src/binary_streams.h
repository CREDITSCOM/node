/**
  * @file binary_streams.h
  * @author Evgeny V. Zalivochkin
  */

#pragma once
#ifndef _CREDITS_CSDB_PRIVATE_BINARY_STREAMS_H_H_INCLUDED_
#define _CREDITS_CSDB_PRIVATE_BINARY_STREAMS_H_H_INCLUDED_

#include <cinttypes>
#include <string>
#include <type_traits>
#include <utility>
#include <map>
#include "csdb/internal/types.h"

#include "integral_encdec.h"

namespace csdb {
namespace priv {

class obstream
{
public:
  void put(const void *buf, size_t size);
  void put(const std::string &value);
  void put(const internal::byte_array &value);

  template<typename T>
  typename std::enable_if<std::is_integral<T>::value || std::is_enum<T>::value, void>::type
  put(T value);

  template<typename T>
  decltype(std::declval<T>().put(std::declval<obstream&>()))
  put(const T& value);

  template<class K, class T, class C, class A>
  void put(const ::std::map<K, T, C, A>& value);

  inline const internal::byte_array &buffer() const { return buffer_; }

private:
  internal::byte_array buffer_;
};

class ibstream
{
public:
  ibstream(const void* data, size_t size) : data_(data), size_(size) {}
  template<typename T>
  explicit inline ibstream(const T& d) : data_(d.data()), size_(d.size()) {}

public:
  bool get(void *buf, size_t size);
  bool get(std::string &value);
  bool get(internal::byte_array &value);

  template<typename T>
  typename std::enable_if<std::is_integral<T>::value || std::is_enum<T>::value, bool>::type
  get(T& value);

  template<typename T>
  decltype(std::declval<T>().get(std::declval<ibstream&>()))
  get(T& value);

  template<class K, class T, class C, class A>
  bool get(::std::map<K, T, C, A>& value);

  inline size_t size() const noexcept
  {
    return size_;
  }

  inline bool empty() const noexcept
  {
    return (0 == size_);
  }

private:
  const void* data_;
  size_t size_;
};

} // namespace priv
} // namespace csdb

namespace csdb {
namespace priv {

template<typename T>
typename std::enable_if<std::is_integral<T>::value || std::is_enum<T>::value, void>::type
inline obstream::put(T value)
{
  uint8_t buf[::csdb::priv::MAX_INTEGRAL_ENCODED_SIZE];
  buffer_.insert(buffer_.end(), buf, buf + ::csdb::priv::encode(buf, value));
}

template<typename T>
decltype(std::declval<T>().put(std::declval<obstream&>()))
inline obstream::put(const T& value)
{
  value.put(*this);
}

template<class K, class T, class C, class A>
void obstream::put(const ::std::map<K, T, C, A>& value)
{
  put(value.size());
  for (const auto& it : value) {
    put(it.first);
    put(it.second);
  }
}

template<typename T>
typename std::enable_if<std::is_integral<T>::value || std::is_enum<T>::value, bool>::type
inline ibstream::get(T& value)
{
  std::size_t res = ::csdb::priv::decode(data_, size_, value);
  if (0 != res) {
    data_ = static_cast<const uint8_t*>(data_) + res;
    size_ -= res;
  }
  return (0 != res);
}

template<typename T>
decltype(std::declval<T>().get(std::declval<ibstream&>()))
inline ibstream::get(T& value)
{
  return value.get(*this);
}

template<class K, class T, class C, class A>
bool ibstream::get(::std::map<K, T, C, A>& value)
{
  value.clear();

  size_t size;
  if (!get(size)) {
    return false;
  }

  for (size_t i = 0; i < size; ++i) {
    K key;
    if (!get(key)) {
      return false;
    }
    T val;
    if (!get(val)) {
      return false;
    }
    value.emplace(key, val);
  }

  return true;
}

} // namespace priv
} // namespace csdb

#endif // _CREDITS_CSDB_PRIVATE_BINARY_STREAMS_H_H_INCLUDED_
