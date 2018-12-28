#include "gtest/gtest.h"
#include "lib/system/concurrent.hpp"

#include <iostream>
#include <string>
#include <atomic>

using ThreadId = std::thread::id;

template<typename T>
using Ref = std::reference_wrapper<T>;

template<typename... Types>
void print(Types&& ...args) {
  (std::cout << ... << std::forward<Types>(args)) << std::endl;
}

#define GENERATE_THREAD_VALUES()\
  std::atomic<bool> isRunningFinished = false;\
  ThreadId mainId = std::this_thread::get_id();\
  ThreadId concurrentId;\
  print("Main thread id: ", mainId)

TEST(Concurrent, SimpleRunWithBinding) {
  GENERATE_THREAD_VALUES();

  class Demo {
  public:
    void method(const std::string& message, Ref<ThreadId> wrapper, Ref<std::atomic<bool>> finished) {
      wrapper.get() = std::this_thread::get_id();

      print("Concurrent thread id: ", wrapper);
      print(message);

      finished.get() = true;
    }
  };

  Demo demo;
  cs::Concurrent::run(std::bind(&Demo::method, &demo, "Finished", std::ref(concurrentId), std::ref(isRunningFinished)));

  while(!isRunningFinished);

  ASSERT_NE(mainId, concurrentId);
}

TEST(Concurrent, SimpleRunWithoutBinding) {
  GENERATE_THREAD_VALUES();

  class Demo {
  public:
    void method(const std::string& message, Ref<ThreadId> wrapper, Ref<std::atomic<bool>> finished) {
      wrapper.get() = std::this_thread::get_id();

      print("Concurrent thread id: ", wrapper);
      print(message);

      finished.get() = true;
    }
  };

  Demo demo;
  cs::Concurrent::run(&Demo::method, &demo, "Finished", std::ref(concurrentId), std::ref(isRunningFinished));

  while(!isRunningFinished);

  ASSERT_NE(mainId, concurrentId);
}

TEST(Concurrent, SimpleRunLambda) {
  GENERATE_THREAD_VALUES();

  auto lambda = [&] {
    concurrentId = std::this_thread::get_id();

    print("Concurrent thread id: ", concurrentId);
    print("Finished");

    isRunningFinished = true;
  };

  cs::Concurrent::run(lambda);
  while(!isRunningFinished);

  ASSERT_NE(mainId, concurrentId);
}

TEST(Concurrent, VoidFutureWatcherBindedRun) {
  GENERATE_THREAD_VALUES();
  static bool called = false;

  class Demo {
  public:
    void method(const std::string& message, Ref<ThreadId> wrapper, Ref<std::atomic<bool>> finished) {
      wrapper.get() = std::this_thread::get_id();

      print("Concurrent thread id: ", wrapper);
      print(message);

      finished.get() = true;
    }

  public slots:
    void onWatcherFinished() {
      print("Watcher finished slot activated");
      called = true;
    }
  };

  Demo demo;
  std::string message = "Finished";
  auto binder = std::bind(&Demo::method, &demo, message, std::ref(concurrentId), std::ref(isRunningFinished));

  cs::FutureWatcherPtr<void> watcher = cs::Concurrent::run(cs::RunPolicy::ThreadPoolPolicy, std::move(binder));
  cs::Connector::connect(&(watcher->finished), &demo, &Demo::onWatcherFinished);

  while(!isRunningFinished);
  std::this_thread::sleep_for(std::chrono::milliseconds(1000));

  ASSERT_NE(mainId, concurrentId);
  ASSERT_EQ(called, true);
}

TEST(Concurrent, VoidFutureWatcherNonBindedRun) {
  GENERATE_THREAD_VALUES();
  static bool called = false;

  class Demo {
  public:
    void method(const std::string& message, Ref<ThreadId> wrapper, Ref<std::atomic<bool>> finished) {
      wrapper.get() = std::this_thread::get_id();

      print("Concurrent thread id: ", wrapper);
      print(message);

      finished.get() = true;
    }

  public slots:
    void onWatcherFinished() {
      print("Watcher finished slot activated");
      called = true;
    }
  };

  Demo demo;
  std::string message = "Finished";

  cs::FutureWatcherPtr<void> watcher = cs::Concurrent::run(cs::RunPolicy::ThreadPoolPolicy, &Demo::method, &demo, message, std::ref(concurrentId), std::ref(isRunningFinished));
  cs::Connector::connect(&(watcher->finished), &demo, &Demo::onWatcherFinished);

  while(!isRunningFinished);
  std::this_thread::sleep_for(std::chrono::milliseconds(1000));

  ASSERT_NE(mainId, concurrentId);
  ASSERT_EQ(called, true);
}
