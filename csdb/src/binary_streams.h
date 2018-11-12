/**
  * @file binary_streams.h
  * @author Evgeny V. Zalivochkin
  */

#pragma once
#ifndef _CREDITS_CSDB_PRIVATE_BINARY_STREAMS_H_H_INCLUDED_
#define _CREDITS_CSDB_PRIVATE_BINARY_STREAMS_H_H_INCLUDED_

#include <string>
#include <type_traits>
#include <map>
#include "csdb/internal/types.h"

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

  template<class K, class T, class C, class A>
  void put_smart(const ::std::map<K, T, C, A>& value);

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
  buffer_.insert(buffer_.end(), reinterpret_cast<uint8_t *>(&value),
    reinterpret_cast<uint8_t *>(&value) + sizeof(value));
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
  put((uint8_t)value.size());
  for (const auto& it : value) {
    put(it.first);
    put(it.second);
  }
}

template<class K, class T, class C, class A>
void obstream::put_smart(const ::std::map<K, T, C, A>& value)
{
	put(static_cast<uint8_t>(value.size()));
	for (const auto& it : value) {
		//put(it.first);
		put(it.second);
	}
}

template<typename T>
typename std::enable_if<std::is_integral<T>::value || std::is_enum<T>::value, bool>::type
inline ibstream::get(T& value)
{
  if (size_ >= sizeof(T))
  {
    value = *(reinterpret_cast<const T*>(data_));
    data_ = static_cast<const uint8_t*>(data_) + sizeof(T);
    size_ -= sizeof(T);
    return true;
  }
  return false;
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

  uint8_t size;
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
