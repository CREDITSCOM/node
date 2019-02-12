#ifndef CONCURRENT_HPP
#define CONCURRENT_HPP

#include <functional>
#include <thread>
#include <future>
#include <memory>
#include <type_traits>
#include <assert.h>
#include <condition_variable>
#include <list>
#include <thread>
#include <unordered_map>

#include <lib/system/signals.hpp>
#include <lib/system/cache.hpp>
#include <lib/system/common.hpp>
#include <lib/system/logger.hpp>

#include <boost/asio/post.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/bind.hpp>

namespace cs {
enum class RunPolicy : cs::Byte {
  CallQueuePolicy,
  ThreadPoolPolicy
};

enum class WatcherState : cs::Byte {
  Idle,
  Running,
  Compeleted
};

// aliasing
using Threads = boost::asio::thread_pool;

// returns instance of boost thread pool
class ThreadPool {
public:
  ThreadPool() = delete;
  ~ThreadPool() = default;

  static Threads& instance() noexcept {
    static Threads threadPool(std::thread::hardware_concurrency());
    return threadPool;
  }
};

template <typename T>
class FutureWatcher;
class Concurrent;

// concurrent private helper
class Worker {
private:
  template <typename Func>
  inline static void execute(Func&& function) {
    Threads& threadPool = ThreadPool::instance();
    boost::asio::post(threadPool, std::forward<Func>(function));
  }

  template <typename Func>
  inline static void policyRun(cs::RunPolicy policy, Func&& function) {
    if (policy == cs::RunPolicy::CallQueuePolicy) {
      CallsQueue::instance().insert(std::forward<Func>(function));
    }
    else {
      function();
    }
  }

  template<typename T>
  friend class FutureBase;

  template<typename T>
  friend class FutureWatcher;
  friend class Concurrent;
};

// future entity
template<typename T>
using Future = std::future<T>;

template <typename Result>
class FutureBase {
public:
  using Id = uint64_t;

protected:
  FutureBase() {
    ++producedId;
    id_ = producedId;
    state_ = WatcherState::Idle;
  }

  FutureBase(FutureBase&) = delete;
  ~FutureBase() = default;

  explicit FutureBase(RunPolicy policy, Future<Result>&& future)
  : FutureBase() {
    future_ = std::move(future);
    policy_ = policy;
  }

  FutureBase(FutureBase&& watcher)
  : future_(std::move(watcher.future_))
  , policy_(watcher.policy_)
  , state_(watcher.state_) {
  }

  FutureBase& operator=(FutureBase&& watcher) {
    if (state_ == WatcherState::Running) {
      cserror() << csname() << "Trying to use operator= in watcher running state";
    }

    future_ = std::move(watcher.future_);
    policy_ = watcher.policy_;
    id_ = watcher.id_;

    return *this;
  }

public:

  // returns current watcher state, if watcher never watched runnable entity
  // then his state is Idle
  WatcherState state() const noexcept {
    return state_;
  }

  Id id() const noexcept {
    return id_;
  }

protected:
  Future<Result> future_;
  RunPolicy policy_;
  WatcherState state_ = WatcherState::Idle;
  Id id_;

  inline static Id producedId = 0;
  constexpr static std::chrono::milliseconds kAwaiterTime { 10 };

  template <typename Func>
  void callSignal(Func&& func) {
    Worker::policyRun(policy_, std::forward<Func>(func));

    future_ = Future<Result>();
    state_ = WatcherState::Compeleted;
  }

  template <typename Awaiter>
  void await(const Awaiter& awaiter) {
    if (cs::Connector::callbacks(&awaiter) != 0) {
      return;
    }

    std::this_thread::sleep_for(kAwaiterTime);
  }
};

// object to get future result from concurrent
// and generate signal when finished
template<typename Result>
class FutureWatcher : public FutureBase<Result> {
public:
  using FinishSignal = cs::Signal<void(Result)>;
  using FailedSignal = cs::Signal<void()>;

  explicit FutureWatcher(RunPolicy policy, Future<Result>&& future)
  : FutureBase<Result>(policy, std::move(future)) {
    watch();
  }

  FutureWatcher() = default;
  ~FutureWatcher() = default;
  FutureWatcher(FutureWatcher&&) = default;

  FutureWatcher& operator=(FutureWatcher&& watcher) {
    FutureBase<Result>::operator=(std::move(watcher));

    watch();
    return  *this;
  }

protected:
  using Super = FutureBase<Result>;

  void watch() {
    auto closure = [=] {
      try {
        Result result = Super::future_.get();

        auto lambda = [this](const Result& result) {
          Super::await(finished);
          emit finished(result);
        };

        Super::callSignal(std::bind(lambda, std::move(result)));
      }
      catch (std::exception& e) {
        Super::await(failed);

        cserror() << "Concurrent execution with " << typeid(Result).name() << " failed, " << e.what();
        emit failed();
      }
    };

    Super::state_ = WatcherState::Running;
    Worker::execute(std::move(closure));
  }

public signals:
  FinishSignal finished;
  FailedSignal failed;
};

template<>
class FutureWatcher<void> : public FutureBase<void> {
public:
  using FinishSignal = cs::Signal<void()>;
  using FailedSignal = cs::Signal<void()>;

  explicit FutureWatcher(RunPolicy policy, Future<void>&& future)
  : FutureBase<void>(policy, std::move(future)) {
    watch();
  }

  FutureWatcher() = default;
  ~FutureWatcher() = default;
  FutureWatcher(FutureWatcher&& watcher) = default;

  FutureWatcher& operator=(FutureWatcher&& watcher) {
    FutureBase<void>::operator=(std::move(watcher));

    watch();
    return *this;
  }

protected:
  using Super = FutureBase<void>;

  void watch() {
    auto closure = [=] {
      try {
        Super::future_.get();

        auto signal = [=] {
          Super::await(finished);
          emit finished();
        };

        Super::callSignal(signal);
      }
      catch (std::exception& e) {
        Super::await(failed);

        cserror() << "Concurrent execution with void result failed, " << e.what();
        emit failed();
      }
    };

    Super::state_ = WatcherState::Running;
    Worker::execute(std::move(closure));
  }

public signals:
  FinishSignal finished;
  FailedSignal failed;
};

// safe pointer to watcher
template<typename T>
using FutureWatcherPtr = std::shared_ptr<FutureWatcher<T>>;

class Concurrent {
public:
  // runs function in another thread, returns future watcher
  // that generates finished signal by run policy
  // if does not stoge watcher object, then main thread will wait async entity in blocking mode
  template<typename Func, typename... Args>
  static FutureWatcherPtr<std::invoke_result_t<std::decay_t<Func>, std::decay_t<Args>...>> run(RunPolicy policy, Func&& function, Args&&... args) {
    using ReturnType = std::invoke_result_t<std::decay_t<Func>, std::decay_t<Args>...>;
    using WatcherType = FutureWatcher<ReturnType>;

    return std::make_shared<WatcherType>(policy, std::async(std::launch::async, std::forward<Func>(function), std::forward<Args>(args)...));
  }

  // runs function entity in thread pool
  template<typename Func>
  static void run(Func&& function) {
    Worker::execute(std::forward<Func>(function));
  }

  // runs non-binded function in thread pool
  template<typename Func, typename... Args>
  static void run(Func&& func, Args&&... args) {
    Concurrent::run(std::bind(std::forward<Func>(func), std::forward<Args>(args)...));
  }

  // calls std::function after ms time in another thread
  static void runAfter(const std::chrono::milliseconds& ms, cs::RunPolicy policy, const std::function<void()>& callBack) {
    auto timePoint = std::chrono::steady_clock::now() + ms;
    Worker::execute(std::bind(&Concurrent::runAfterHelper, timePoint, policy, callBack));
  }

private:
  static void runAfterHelper(const std::chrono::steady_clock::time_point& timePoint, cs::RunPolicy policy, const std::function<void()>& callBack) {
    std::this_thread::sleep_until(timePoint);
    Worker::policyRun(policy, callBack);
  }
};

template<typename T>
inline bool operator<(const FutureWatcher<T>& lhs, const FutureWatcher<T>& rhs) {
  return lhs.id() < rhs.id();
}

template<typename T>
inline bool operator==(const FutureWatcher<T>& watcher, uint64_t value) {
  return watcher.id() == value;
}

// for api threading
template <typename S>
struct WorkerQueue {
private:
  using tids_t = std::list<std::tuple<>>;
  tids_t tids_;
  std::unordered_map<std::thread::id, typename tids_t::iterator> tidMap_;

  std::condition_variable_any conditionalVariable_;
  cs::SpinLock lock_;
  S state_;

public:
  inline WorkerQueue() noexcept : lock_() {}

  S get_state() const {
    return state_;
  }

  void get_position() {
    std::lock_guard<decltype(lock_)> l(lock_);
    auto tid = std::this_thread::get_id();
    tidMap_[tid] = tids_.insert(tids_.end(), std::make_tuple());
  }

  template <typename J>
  void wait_till_front(const J& j) {
    std::unique_lock<decltype(lock_)> l(lock_);

    conditionalVariable_.wait(l, [&]() { return j(state_); });

    tidMap_.erase(std::this_thread::get_id());
        conditionalVariable_.notify_all();
  }

  void yield() {
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
  }

  template <typename State>
  void update_state(const State& state) {
    std::lock_guard<decltype(lock_)> lock(lock_);
    state_ = state(state_);
    conditionalVariable_.notify_all();
  }
};

// what is sweet spot?
struct SweetSpot {
private:
  std::condition_variable_any conditionalVariable_;
  cs::SpinLock lock_;
  bool occupied_ = false;

public:
  inline SweetSpot() noexcept : lock_() {}

  void occupy() {
    std::unique_lock<decltype(lock_)> lock(lock_);

    conditionalVariable_.wait(lock, [this]() {
      auto res = !occupied_;
      occupied_ = true;
      return res;
    });
  }
  void leave() {
    std::lock_guard<decltype(lock_)> lock(lock_);
    occupied_ = false;
    conditionalVariable_.notify_one();
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
}
#endif  //  CONCURRENT_HPP
