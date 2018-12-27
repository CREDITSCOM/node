#ifndef CONCURRENT_HPP
#define CONCURRENT_HPP

#include <functional>
#include <future>
#include <type_traits>

#include <lib/system/structures.hpp>
#include <lib/system/signals.hpp>

#include <boost/asio/post.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/bind.hpp>

namespace cs {
enum class RunPolicy {
  CallQueuePolicy,
  ThreadPoolPolicy
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
  FutureWatcher() = default;
  ~FutureWatcher() = default;

  FutureWatcher(FutureWatcher&) = delete;
  FutureWatcher(FutureWatcher&&) = delete;

  explicit FutureWatcher(RunPolicy policy, Future<Result>& future)
  : future_(std::move(future))
  , policy_(policy) {
    watch();
  }

  FutureWatcher& operator=(FutureWatcher&& watcher) {
    future_ = std::move(watcher.future_);
    policy_ = watcher.policy_;
    watch();
  }

private:
  Future<Result> future_;
  RunPolicy policy_;

  void watch() {
    auto closure = [=] {
      Result result = future_.get();

      auto signal = [=] {
        emit finished_(result);
      };

      // insert in call queue or call direct
      if (policy_ == RunPolicy::CallQueuePolicy) {
        CallsQueue::instance().insert(signal);
      }
      else {
        signal();
      }

      future_ = Future<Result>();
    };

    Worker::execute(closure);
  }

public signals:
  cs::Signal<void(Result)> finished_;
};

class Concurrent {
public:
  // runs function in another thread, returns future watcher
  // that generates finished signal by run policy
  template<typename Result, typename... Args>
  static FutureWatcher<Result> run(RunPolicy policy, const std::function<Result(Args...)>& function) {
    return FutureWatcher(policy, std::async(std::launch::async, function));
  }

  // runs not binded function with args async
  template<typename Function, typename... Args>
  static FutureWatcher<std::invoke_result_t<std::decay_t<Function>, std::decay_t<Args>...>> run(RunPolicy policy, Function&& function, Args&&... args) {
    return Concurrent::run(policy, std::bind(std::forward<Function>(function), args...));
  }

  // runs function entity in thread pool
  template<typename Func>
  static void run(Func&& function) {
    Worker::execute(std::forward<Func>(function));
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
}
#endif  //  CONCURRENT_HPP
