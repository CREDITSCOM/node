#ifndef CONCURRENT_HPP
#define CONCURRENT_HPP

#include <functional>
#include <future>
#include <type_traits>
#include <memory>

#include <lib/system/structures.hpp>
#include <lib/system/signals.hpp>

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

  template<typename T>
  friend class FutureWatcher;
  friend class Concurrent;
};

// future entity
template<typename T>
using Future = std::future<T>;

// object to get future result from concurrent
// and generate signal when finished
template<typename Result>
class FutureWatcher {
public:
  using FinishSignal = cs::Signal<void(Result)>;
  using FailedSignal = cs::Signal<void()>;
  using Id = uint64_t;

  FutureWatcher() {
    ++producedId;
    id_ = producedId;
    state_ = WatcherState::Idle;
  }

  FutureWatcher(FutureWatcher&) = delete;
  ~FutureWatcher() = default;

  explicit FutureWatcher(RunPolicy policy, Future<Result>&& future)
  : FutureWatcher() {
    future_ = std::move(future);
    policy_ = policy;

    watch<Result>();
  }

  FutureWatcher(FutureWatcher&& watcher)
  : future_(std::move(watcher.future_))
  , policy_(watcher.policy_)
  , state_(watcher.state_) {
  }

  FutureWatcher& operator=(FutureWatcher&& watcher) {
    if (state_ == WatcherState::Running) {
      cserror() << csname() << "Trying to use operator= in watcher running state";
    }

    future_ = std::move(watcher.future_);
    policy_ = watcher.policy_;
    id_ = watcher.id_;

    watch<Result>();
  }

  // returns current watcher state, if watcher never watched runnable entity
  // then his state is Idle
  WatcherState state() const noexcept {
    return state_;
  }

  Id id() const noexcept {
    return id_;
  }

private:
  Future<Result> future_;
  RunPolicy policy_;
  WatcherState state_ = WatcherState::Idle;
  Id id_;

  inline static Id producedId = 0;

  template<typename T>
  void watch() {
    auto closure = [=] {
      try {
        T result = future_.get();

        auto signal = [=] {
          emit finished(result);
        };

        callSignal(signal);
      }
      catch (std::exception& e) {
        cserror() << "Concurrent execution with" << typeid(T).name() << "failed, " << e.what();
        emit failed();
      }
    };

    state_ = WatcherState::Running;
    Worker::execute(closure);
  }

  template <>
  void watch<void>() {
    auto closure = [=] {
      try {
        future_.get();

        auto signal = [=] {
          emit finished();
        };

        callSignal(signal);
      }
      catch (std::exception& e) {
        cserror() << "Concurrent execution with void result failed, " << e.what();
        emit failed();
      }
    };

    state_ = WatcherState::Running;
    Worker::execute(closure);
  }

  template <typename Func>
  void callSignal(Func&& func) {
    // insert in call queue or call direct
    if (policy_ == RunPolicy::CallQueuePolicy) {
      CallsQueue::instance().insert(std::forward<Func>(func));
    }
    else {
      func();
    }

    future_ = Future<Result>();
    state_ = WatcherState::Compeleted;
  }

public signals:
  FinishSignal finished;
  FailedSignal failed;
};

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
    return FutureWatcherPtr<ReturnType>(new FutureWatcher<ReturnType>(policy, std::async(std::launch::async, std::forward<Func>(function), std::forward<Args>(args)...)));
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
  static void runAfter(const std::chrono::milliseconds& ms, const std::function<void()>& callBack) {
    auto timePoint = std::chrono::steady_clock::now() + ms;
    Worker::execute(std::bind(&Concurrent::runAfterHelper, timePoint, callBack));
  }

private:
  static void runAfterHelper(const std::chrono::steady_clock::time_point& timePoint, const std::function<void()>& callBack) {
    std::this_thread::sleep_until(timePoint);

    // TODO: call callback without Queue
    CallsQueue::instance().insert(callBack);
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
}
#endif  //  CONCURRENT_HPP
