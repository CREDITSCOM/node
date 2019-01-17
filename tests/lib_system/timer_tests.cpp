#include "gtest/gtest.h"

#include <string>
#include <atomic>

#include "lib/system/timer.hpp"
#include "lib/system/utils.hpp"

template<typename... Types>
void print(Types&& ...args) {
  (std::cout << ... << std::forward<Types>(args)) << std::endl;
}

TEST(Timer, BaseTimerUsage) {
  static std::atomic<bool> isCalled = false;
  static std::atomic<bool> isFailed = false;
  static std::atomic<size_t> counter = 0;
  static size_t expectedCalls = 10;
  static int awaitTime = 10000;

  class Demo {
  public slots:
    void onTick() {
      print("Timer tick done", ++counter);
      isCalled = true;
    }
  };

  Demo demo;
  cs::Timer timer;

  cs::Connector::connect(&timer.timeOut, &demo, &Demo::onTick);

  // run timer
  timer.start(100);

  cs::Timer::singleShot(awaitTime, cs::RunPolicy::ThreadPoolPolicy, [&] {
    if (counter != expectedCalls) {
      print("Timer bad call, counts: ", counter);
      isFailed = true;
    }
  });

  while (counter != expectedCalls && !isCalled) {
    if (isFailed) {
      break;
    }
  }

  print("Time point before timer stop: ", cs::Utils::formattedCurrentTime());

  timer.stop();

  print("Time point after timer stop: ", cs::Utils::formattedCurrentTime());

  ASSERT_EQ(isCalled, true);
  ASSERT_EQ(isFailed, false);
}

TEST(Timer, HighPreciseTimerUsage) {
  static std::atomic<bool> isCalled = false;
  static std::atomic<bool> isFailed = false;
  static std::atomic<size_t> counter = 0;
  static size_t expectedCalls = 10;
  static int timeTickTime = 200;
  static int awaitTime = 10000;
  static std::chrono::high_resolution_clock::time_point timePoint;

  class A {
  public slots:
    void onTimerTick() {
      auto now = std::chrono::high_resolution_clock::now();
      auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - timePoint);

      ++counter;
      isCalled = true;

      print("Time elapsed: ", ms.count(), ", counter: ", counter);
    }
  };

  A a;
  cs::Timer timer;
  cs::Connector::connect(&timer.timeOut, &a, &A::onTimerTick);

  cs::Timer::singleShot(awaitTime, cs::RunPolicy::ThreadPoolPolicy, [&] {
    if (counter != expectedCalls) {
      print("Timer bad call, counts: ", counter);
      isFailed = true;
    }
  });

  while (expectedCalls != counter && !isFailed) {
    timePoint = std::chrono::high_resolution_clock::now();
    timer.start(timeTickTime, cs::Timer::Type::HighPrecise);

    while (!isCalled && !isFailed);

    isCalled = false;
    timer.stop();

    print("Wait Cycle iteration");
  }

  print("Left main await cycle");

  ASSERT_EQ(expectedCalls, counter);
  ASSERT_EQ(isFailed, false);
}
