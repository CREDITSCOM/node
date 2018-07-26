// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
//
// This file contains the specification, but not the implementations,
// of the types/operations/etc. that should be defined by a platform
// specific port_<platform>.h file.  Use this file as a reference for
// how to port this package to a new platform.

#ifndef STORAGE_LEVELDB_PORT_PORT_EXAMPLE_H_
#define STORAGE_LEVELDB_PORT_PORT_EXAMPLE_H_

#include <type_traits>
#include <mutex>
#include <atomic>

#ifdef HAVE_SNAPPY
#include <snappy.h>
#endif  // defined(HAVE_SNAPPY)

namespace leveldb {
namespace port {

// TODO(jorlow): Many of these belong more in the environment class rather than
//               here. We should try moving them and see if it affects perf.

// The following boolean constant must be true on a little-endian machine
// and false otherwise.
static const bool kLittleEndian = true /* or some other expression */;

// ------------------ Threading -------------------

// A Mutex represents an exclusive lock.
class Mutex final {
public:
  Mutex();
  ~Mutex();

  // Lock the mutex.  Waits until other lockers have exited.
  // Will deadlock if the mutex is already locked by this thread.
  void Lock();

  // Unlock the mutex.
  // REQUIRES: This mutex was locked by this thread.
  void Unlock();

  // Optionally crash if this thread does not hold this mutex.
  // The implementation must be fast, especially if NDEBUG is
  // defined.  The implementation is allowed to skip all checks.
  inline void AssertHeld() {}

private:
  void* m_;
  friend class CondVar;
};

class CondVar final {
public:
  explicit CondVar(Mutex* mu);
  ~CondVar();

  // Atomically release *mu and block on this condition variable until
  // either a call to SignalAll(), or a call to Signal() that picks
  // this thread to wakeup.
  // REQUIRES: this thread holds *mu
  void Wait();

  // If there are some threads waiting, wake up at least one of them.
  void Signal();

  // Wake up all waiting threads.
  void SignalAll();

private:
  void* cv_;
  Mutex* m_;
};

// Thread-safe initialization.
// Used as follows:
//      static port::OnceType init_control = LEVELDB_ONCE_INIT;
//      static void Initializer() { ... do something ...; }
//      ...
//      port::InitOnce(&init_control, &Initializer);
class OnceType final : public std::once_flag {
public:
  inline OnceType(std::nullptr_t) {}
};
#define LEVELDB_ONCE_INIT nullptr
inline void InitOnce(port::OnceType* flag, void (*initializer)()) {
  std::call_once(*flag, initializer);
}

// A type that holds a pointer that can be read or written atomically
// (i.e., without word-tearing.)
class AtomicPointer final : std::atomic<void*> {
  typedef std::atomic<void*> _Base;
public:
  inline AtomicPointer() {}
  inline AtomicPointer(void* v) : _Base(v) {}

  // Read and return the stored pointer with the guarantee that no
  // later memory access (read or write) by this thread can be
  // reordered ahead of this read.
  inline void* Acquire_Load() const {
    return _Base::load();
  }

  // Set v as the stored pointer with the guarantee that no earlier
  // memory access (read or write) by this thread can be reordered
  // after this store.
  inline void Release_Store(void* v) {
    _Base::store(v);
  }

  // Read the stored pointer with no ordering guarantees.
  inline void* NoBarrier_Load() const {
    return _Base::load();
  }

  // Set va as the stored pointer with no ordering guarantees.
  inline void NoBarrier_Store(void* v) {
    _Base::store(v);
  }
};

// ------------------ Compression -------------------
inline bool Snappy_Compress(const char* input, size_t length,
                            ::std::string* output) {
#ifdef HAVE_SNAPPY
  output->resize(snappy::MaxCompressedLength(length));
  size_t outlen;
  snappy::RawCompress(input, length, &(*output)[0], &outlen);
  output->resize(outlen);
  return true;
#else  // defined(HAVE_SNAPPY)
  ((void)input);
  ((void)length);
  ((void)output);
#endif  // defined(HAVE_SNAPPY)

  return false;
}

inline bool Snappy_GetUncompressedLength(const char* input, size_t length,
                                         size_t* result) {
#ifdef HAVE_SNAPPY
  return snappy::GetUncompressedLength(input, length, result);
#else
  ((void)input);
  ((void)length);
  ((void)result);
  return false;
#endif  // defined(HAVE_SNAPPY)
}

inline bool Snappy_Uncompress(const char* input, size_t length,
                              char* output) {
#ifdef HAVE_SNAPPY
  return snappy::RawUncompress(input, length, output);
#else
  ((void)input);
  ((void)length);
  ((void)output);
  return false;
#endif  // defined(HAVE_SNAPPY)
}

// ------------------ Miscellaneous -------------------

// If heap profiling is not supported, returns false.
// Else repeatedly calls (*func)(arg, data, n) and then returns true.
// The concatenation of all "data[0,n-1]" fragments is the heap profile.
inline bool GetHeapProfile(void (* /*func*/)(void*, const char*, int), void* /*arg*/)
{
  return false;
}

// Extend the CRC to include the first n bytes of buf.
//
// Returns zero if the CRC cannot be extended using acceleration, else returns
// the newly extended CRC value (which may also be zero).
uint32_t AcceleratedCRC32C(uint32_t crc, const char* buf, size_t size);

}  // namespace port
}  // namespace leveldb

typedef std::make_signed<size_t>::type ssize_t;

#define strdup _strdup

#endif  // STORAGE_LEVELDB_PORT_PORT_EXAMPLE_H_
