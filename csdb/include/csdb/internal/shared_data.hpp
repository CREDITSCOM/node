/**
 * @file shared_data.h
 * @author Evgeny V. Zalivochkin
 */

#ifndef _CREDITS_CSDB_SHARED_DATA_H_INCLUDED
#define _CREDITS_CSDB_SHARED_DATA_H_INCLUDED

#include <atomic>
#include <type_traits>

namespace csdb {
namespace internal {

template <class T>
class shared_data_ptr {
public:
  shared_data_ptr(const shared_data_ptr<T>& o) noexcept;
  shared_data_ptr(shared_data_ptr<T>&&) noexcept;
  explicit shared_data_ptr(T* data) noexcept;
  ~shared_data_ptr();

public:
  inline const T& operator*() const {
    return *d;
  }
  inline const T* operator->() const {
    return d;
  }
  inline T& operator*() {
    detach();
    return *d;
  }
  inline T* operator->() {
    detach();
    return d;
  }
  inline operator T*() {
    detach();
    return d;
  }
  inline operator const T*() const {
    return d;
  }
  inline T* data() {
    detach();
    return d;
  }
  inline const T* data() const {
    return d;
  }
  inline const T* constData() const {
    return d;
  }

  inline bool operator==(const shared_data_ptr<T>& other) const {
    return d == other.d;
  }
  inline bool operator!=(const shared_data_ptr<T>& other) const {
    return d != other.d;
  }

  shared_data_ptr<T>& operator=(const shared_data_ptr<T>&) noexcept;
  shared_data_ptr<T>& operator=(shared_data_ptr<T>&&) noexcept;

private:
  inline void detach();

private:
  T* d = nullptr;
};

static_assert((sizeof(shared_data_ptr<int>) == sizeof(void*)), "Invalid csdb::shared_data_ptr definition.");

}  // namespace internal
}  // namespace csdb

#ifdef CSDB_UNIT_TEST
#define UNIT_TEST_METHODS_DECLARE  \
public:                            \
  bool copy_semantic_used() const; \
  bool move_semantic_used() const;
#else
#define UNIT_TEST_METHODS_DECLARE
#endif

#define SHARED_DATA_CLASS_DECLARE(class_name)        \
public:                                              \
  class_name();                                      \
  class_name(const class_name&) noexcept;            \
  class_name(class_name&&) noexcept;                 \
  class_name& operator=(const class_name&) noexcept; \
  class_name& operator=(class_name&&) noexcept;      \
  ~class_name();                                     \
  class_name clone() const;                          \
  UNIT_TEST_METHODS_DECLARE                          \
private:                                             \
  class priv;                                        \
  class_name(priv* source);                          \
  ::csdb::internal::shared_data_ptr<priv> d;

#endif  // _CREDITS_CSDB_SHARED_DATA_H_INCLUDED
