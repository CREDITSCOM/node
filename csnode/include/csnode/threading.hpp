#ifndef THREADING_HPP
#define THREADING_HPP

#include <assert.h>
#include <client/params.hpp>
#include <lib/system/cache.hpp>
#include <lib/system/common.hpp>
#include <lib/system/logger.hpp>

#define BOTTLENECKED_SMARTS

#include <condition_variable>
#include <list>
#include <thread>
#include <unordered_map>

namespace cs {
template <typename S>
struct WorkerQueue {
private:
#ifdef BOTTLENECKED_SMARTS
  using tids_t = std::list<std::tuple<>>;
  tids_t tids_;
  std::unordered_map<std::thread::id, typename tids_t::iterator> tidMap_;
#endif
  std::condition_variable_any conditionalVariable_;
  cs::SpinLock lock_;
  S state_;

public:
  inline WorkerQueue() noexcept : lock_() {}

  S get_state() const {
    return state_;
  }

  void get_position() {
#ifdef BOTTLENECKED_SMARTS

    std::lock_guard<decltype(lock_)> l(lock_);
    auto tid = std::this_thread::get_id();
    tidMap_[tid] = tids_.insert(tids_.end(), std::make_tuple());
#endif
  }

  template <typename J>
  void wait_till_front(const J& j) {
    std::unique_lock<decltype(lock_)> l(lock_);
#ifdef BOTTLENECKED_SMARTS

    auto tid = std::this_thread::get_id();
    auto tit = tidMap_.find(tid);
    assert(tit != tidMap_.end());

#endif
    conditionalVariable_.wait(l, [&]() {
      if (
#ifdef BOTTLENECKED_SMARTS
          tit->second == tids_.begin() &&
#endif
          j(state_)) {
#ifdef BOTTLENECKED_SMARTS
        tids_.pop_front();
        tidMap_.erase(tit);
        conditionalVariable_.notify_all();

#endif
        return true;
      }

      return false;
    });
  }

  void yield() {
#ifdef BOTTLENECKED_SMARTS

    std::unique_lock<decltype(lock_)> l(lock_);
    auto tid = std::this_thread::get_id();
    auto tit = tidMap_.find(tid);
    if (tit == tidMap_.end()) {
      return;
    }

    bool needNotifyAll = tit->second == tids_.begin();
    tids_.erase(tit->second);

    tidMap_.erase(tit);

    if (needNotifyAll) {
      conditionalVariable_.notify_all();
    }

#endif
  }

  template <typename J>
  void update_state(const J& j) {
    std::lock_guard<decltype(lock_)> l(lock_);
    state_ = j();
    conditionalVariable_.notify_all();
  }
};

// what is sweet spot?
struct SweetSpot {
#ifdef BOTTLENECKED_SMARTS
private:
  std::condition_variable_any conditionalVariable_;
  cs::SpinLock lock_;
  bool occupied_ = false;
#endif

public:
  inline SweetSpot() noexcept : lock_() {}

  void occupy() {
#ifdef BOTTLENECKED_SMARTS
    std::unique_lock<decltype(lock_)> l(lock_);
    conditionalVariable_.wait(l, [this]() {
      auto res = !occupied_;
      occupied_ = true;
      return res;
    });
#endif
  }
  void leave() {
#ifdef BOTTLENECKED_SMARTS
    std::lock_guard<decltype(lock_)> l(lock_);
    occupied_ = false;
    conditionalVariable_.notify_one();
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
  SpinLockable<T>* lockable_;

public:
  SpinLockedRef(SpinLockable<T>& lockable)
  : lockable_(&lockable) {
    while (this->lockable_->af.test_and_set(std::memory_order_acquire));
  }

  ~SpinLockedRef() {
    if (lockable_) {
      lockable_->af.clear(std::memory_order_release);
    }
  }

  SpinLockedRef(const SpinLockedRef&) = delete;
  SpinLockedRef(SpinLockedRef&& other)
  : lockable_(other.lockable_) {
    other.lockable_ = nullptr;
  }

  operator T*() {
    return &(lockable_->t);
  }

  T* operator->() {
    return *this;
  }

  T& operator*() {
    return lockable_->t;
  }
};

template <typename T>
SpinLockedRef<T> lockedReference(SpinLockable<T>& lockable) {
  return SpinLockedRef<T>(lockable);
}

}  // namespace cs
#endif
