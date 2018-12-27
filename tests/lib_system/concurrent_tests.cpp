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

TEST(Concurrent, SimpleRunWithBinding) {
  std::atomic<bool> isRunningFinished = false;

  ThreadId mainId = std::this_thread::get_id();
  ThreadId concurrentId;

  print("Main thread id: ", mainId);

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
  std::atomic<bool> isRunningFinished = false;

  ThreadId mainId = std::this_thread::get_id();
  ThreadId concurrentId;

  print("Main thread id: ", mainId);

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
  std::atomic<bool> isRunningFinished = false;

  ThreadId mainId = std::this_thread::get_id();
  ThreadId concurrentId;

  print("Main thread id: ", mainId);

  auto lambda = [&] {
    concurrentId = std::this_thread::get_id();
    print("Concurrent thread id: ", concurrentId);

    isRunningFinished = true;
  };

  cs::Concurrent::run(lambda);
  while(!isRunningFinished);

  ASSERT_NE(mainId, concurrentId);
}
