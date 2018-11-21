#pragma once

#include <assert.h>
#include <lib/system/common.hpp>
#include <lib/system/logger.hpp>
#include <lib/system/cache.hpp>
#include <client/params.hpp>

#define BOTTLENECKED_SMARTS

#include <condition_variable>
#include <thread>
#include <unordered_map>
#include <list>

namespace cs {
class spinlock {
  __cacheline_aligned std::atomic_flag af = ATOMIC_FLAG_INIT;

public:
  void lock() {
    while (af.test_and_set(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
  }

  void unlock() {
    af.clear(std::memory_order_release);
  }
};

template <typename S>
struct worker_queue {
private:

#ifdef BOTTLENECKED_SMARTS
  typedef std::list<std::tuple<>> tids_t;
  tids_t tids;
  std::unordered_map<std::thread::id, typename tids_t::iterator> tid_map;
#endif
  std::condition_variable_any w;
  cs::spinlock lock;
  S state;

public:
  S get_state() const {
    return state;
  }

  void get_position() {
#ifdef BOTTLENECKED_SMARTS

    std::lock_guard<decltype(lock)> l(lock);
    auto tid = std::this_thread::get_id();
    tid_map[tid] = tids.insert(tids.end(), std::make_tuple());

#endif
  }

  template <typename J>
  void wait_till_front(const J& j) {
    std::unique_lock<decltype(lock)> l(lock);
#ifdef BOTTLENECKED_SMARTS

    auto tid = std::this_thread::get_id();
    auto tit = tid_map.find(tid);
    assert(tit != tid_map.end());

#endif
    w.wait(l, [&]() {
      if (
#ifdef BOTTLENECKED_SMARTS
          tit->second == tids.begin() &&
#endif
          j(state)) {
#ifdef BOTTLENECKED_SMARTS
        tids.pop_front();
        tid_map.erase(tit);
        w.notify_all();

#endif
        return true;
      }

      return false;
    });
  }

  void yield() {
#ifdef BOTTLENECKED_SMARTS

    std::unique_lock<decltype(lock)> l(lock);
    auto tid = std::this_thread::get_id();
    auto tit = tid_map.find(tid);
    if (tit == tid_map.end()) {
      return;
    }

    bool needNotifyAll = tit->second == tids.begin();
    tids.erase(tit->second);

    tid_map.erase(tit);

    if (needNotifyAll) {
      w.notify_all();
    }

#endif
  }

  template <typename J>
  void update_state(const J& j) {
    std::lock_guard<decltype(lock)> l(lock);
    state = j();
    w.notify_all();
  }
};

struct sweet_spot {
#ifdef BOTTLENECKED_SMARTS
private:
  std::condition_variable_any cv;
  cs::spinlock lock;
  bool occupied = false;
#endif

public:
  void occupy() {
#ifdef BOTTLENECKED_SMARTS
    std::unique_lock<decltype(lock)> l(lock);
    cv.wait(l, [this]() {
      auto res = !occupied;
      occupied = true;
      return res;
    });
#endif
  }
  void leave() {
#ifdef BOTTLENECKED_SMARTS
    std::lock_guard<decltype(lock)> l(lock);
    occupied = false;
    cv.notify_one();
#endif
  }
};

template <typename T>
struct SpinLockedRef;

template <typename T>
struct SpinLockable {
  using LockedType = T;

  template <typename... Args>
  SpinLockable(Args&&... args)
  : t(std::forward<Args>(args)...) {
  }

private:
  __cacheline_aligned std::atomic_flag af = ATOMIC_FLAG_INIT;
  T t;

  friend struct SpinLockedRef<T>;
};

template <typename T>
struct SpinLockedRef {
private:
  SpinLockable<T>* l;

public:
  SpinLockedRef(SpinLockable<T>& l)
  : l(&l) {
    while (this->l->af.test_and_set(std::memory_order_acquire))
      ;
  }
  ~SpinLockedRef() {
    if (l)
      l->af.clear(std::memory_order_release);
  }

  SpinLockedRef(const SpinLockedRef&) = delete;
  SpinLockedRef(SpinLockedRef&& other)
  : l(other.l) {
    other.l = nullptr;
  }

  operator T*() {
    return &(l->t);
  }

  T* operator->() {
    return *this;
  }

  T& operator*() {
    return l->t;
  }
};

template <typename T>
SpinLockedRef<T> locked_ref(SpinLockable<T>& l) {
  return SpinLockedRef<T>(l);
}

}  // namespace cs
