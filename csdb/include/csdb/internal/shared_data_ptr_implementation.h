/**
  * @file shared_data_ptr_implementation.h
  * @author Evgeny V. Zalivochkin
  */

#pragma once
#ifndef _CREDITS_CSDB_SHARED_DATA_PTR_IMPLEMENTATION_H_INCLUDED_
#define _CREDITS_CSDB_SHARED_DATA_PTR_IMPLEMENTATION_H_INCLUDED_

#include "shared_data.h"

#include <utility>

namespace csdb {
namespace internal {

class shared_data
{
public:
  inline shared_data() : ref(0) {}
  inline shared_data(const shared_data&) : ref(0) {}

private:
  shared_data(shared_data&&) = delete;
  shared_data &operator=(const shared_data&) = delete;
  shared_data &operator=(shared_data&&) = delete;

public:
  mutable std::atomic<std::size_t> ref;
#ifdef CSDB_UNIT_TEST
  bool copy_semantic_used_ = false;
  bool move_semantic_used_ = false;
#endif
};

template <class T>
inline shared_data_ptr<T>::shared_data_ptr(const shared_data_ptr<T> &o) noexcept : d(o.d)
{
  if(d) {
    if (1 < (++d->ref)) {
#ifdef CSDB_UNIT_TEST
      d->copy_semantic_used_ = true;
#endif
    }
  }
}

template <class T>
inline shared_data_ptr<T>::shared_data_ptr(shared_data_ptr<T>&& o) noexcept : d(o.d)
{
  o.d = nullptr;
#ifdef CSDB_UNIT_TEST
  d->move_semantic_used_ = true;
#endif
}

template <class T>
inline shared_data_ptr<T>::shared_data_ptr(T *data) noexcept : d(data)
{
  if(d) {
    if (1 < (++d->ref)) {
#ifdef CSDB_UNIT_TEST
      d->copy_semantic_used_ = true;
#endif
    }
  }
}

template <class T>
inline shared_data_ptr<T>::~shared_data_ptr()
{
  if (d && (0 == (--d->ref))) {
    delete d;
  }
}

template <class T>
inline shared_data_ptr<T> & shared_data_ptr<T>::operator=(const shared_data_ptr<T> &o) noexcept
{
  if(o.d != d) {
    if(o.d) {
      if (1 < (++o.d->ref)) {
#ifdef CSDB_UNIT_TEST
        o.d->copy_semantic_used_ = true;
#endif
      }
    }
    T *old = d;
    d = o.d;
    if (old && (0 == (--old->ref))) {
      delete old;
    }
  }
  return *this;
}

template <class T>
inline shared_data_ptr<T> & shared_data_ptr<T>::operator=(shared_data_ptr<T> &&o) noexcept
{
  std::swap(d, o.d);
#ifdef CSDB_UNIT_TEST
  d->move_semantic_used_ = true;
#endif
  return *this;
}

template <class T>
inline void shared_data_ptr<T>::detach()
{
  if (d && (d->ref != 1)) {
    T *x = new T(*d);
    ++x->ref;
    if(0 == (--d->ref)) {
      delete d;
    }
    d = x;
  }
}

} // namespace internal
} // namespace csdb

#ifdef CSDB_UNIT_TEST
# define UNIT_TEST_METHODS_IMPLEMENTATION(class_name) \
  bool class_name::copy_semantic_used() const { return d->copy_semantic_used_; } \
  bool class_name::move_semantic_used() const { return d->move_semantic_used_; }
#else
# define UNIT_TEST_METHODS_IMPLEMENTATION(class_name)
#endif

#define SHARED_DATA_CLASS_IMPLEMENTATION(class_name) \
  template class ::csdb::internal::shared_data_ptr<class_name::priv>; \
  class_name::class_name() : d(new class_name::priv) { \
    static_assert(std::is_base_of<::csdb::internal::shared_data, class_name::priv>::value, \
    "class `" #class_name "::priv` must be derived from `::csdb::internal::shared_data`."); \
  } \
  class_name::class_name(const class_name &o) noexcept : d(o.d) {} \
  class_name::class_name(class_name &&o) noexcept : d(std::move(o.d)) {} \
  class_name& class_name::operator =(const class_name &o) noexcept { if( &o != this ) { d = o.d; } return *this; } \
  class_name& class_name::operator =(class_name &&o) noexcept { d = std::move(o.d); return *this; } \
  class_name::class_name(priv* source) : d(source) {} \
  class_name::~class_name() {} \
  UNIT_TEST_METHODS_IMPLEMENTATION(class_name)

#endif // _CREDITS_CSDB_SHARED_DATA_PTR_IMPLEMENTATION_H_INCLUDED_
