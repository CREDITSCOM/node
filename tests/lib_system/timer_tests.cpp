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

  cs::Timer::singleShot(awaitTime, [&] {
    if (counter != expectedCalls) {
      print("Timer bad call, counts: ", counter);
      isFailed = true;
    }
  });

  while(counter != expectedCalls && !isFailed);

  print("Time point before timer stop: ", cs::Utils::formattedCurrentTime());

  timer.stop();

  print("Time point after timer stop: ", cs::Utils::formattedCurrentTime());

  ASSERT_EQ(isCalled, true);
  ASSERT_EQ(isFailed, false);
}
