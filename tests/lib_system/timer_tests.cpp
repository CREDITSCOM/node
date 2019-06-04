#include "gtest/gtest.h"

#include <string>
#include <atomic>

#include "lib/system/timer.hpp"
#include "lib/system/utils.hpp"

TEST(Timer, BaseTimerUsage) {
    static std::atomic<bool> isCalled = false;
    static std::atomic<bool> isFailed = false;
    static std::atomic<bool> isChecked = false;
    static std::atomic<size_t> counter = 0;
    static size_t expectedCalls = 10;
    static int awaitTime = 2500;

    class Demo {
    public slots:
        void onTick() {
            if (!isCalled) {
                cs::Console::writeLine("Timer tick done", ++counter);
            }

            if (counter == expectedCalls) {
                isCalled = true;
            }
        }
    };

    Demo demo;
    cs::Timer timer;

    cs::Connector::connect(&timer.timeOut, &demo, &Demo::onTick);

    // run timer
    timer.start(100);

    cs::Timer::singleShot(awaitTime, cs::RunPolicy::ThreadPolicy, [&] {
        if (counter != expectedCalls) {
            cs::Console::writeLine("Timer bad call, counts: ", counter);
            isFailed = true;
        }

        isChecked = true;
    });

    while (counter != expectedCalls) {
        if (isFailed) {
            break;
        }
    }

    cs::Console::writeLine("Time point before timer stop: ", cs::Utils::formattedCurrentTime());

    timer.stop();

    cs::Console::writeLine("Time point after timer stop: ", cs::Utils::formattedCurrentTime());

    while (!isChecked);

    ASSERT_EQ(isCalled, true);
    ASSERT_EQ(isFailed, false);
}

TEST(Timer, HighPreciseTimerUsage) {
    static std::atomic<bool> isCalled = false;
    static std::atomic<bool> isFailed = false;
    static std::atomic<bool> isChecked = false;
    static std::atomic<size_t> counter = 0;
    static size_t expectedCalls = 10;
    static int timeTickTime = 200;
    static int awaitTime = 3000;
    static std::chrono::high_resolution_clock::time_point timePoint;

    class A {
    public slots:
        void onTimerTick() {
            auto now = std::chrono::high_resolution_clock::now();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - timePoint);

            ++counter;
            isCalled = true;

            cs::Console::writeLine("Time elapsed: ", ms.count(), ", counter: ", counter);
        }
    };

    A a;
    cs::Timer timer;
    cs::Connector::connect(&timer.timeOut, &a, &A::onTimerTick);

    cs::Timer::singleShot(awaitTime, cs::RunPolicy::ThreadPolicy, [&] {
        cs::Console::writeLine("Timer values, counter ", counter, ", expectedCalls ", expectedCalls);

        if (counter != expectedCalls) {
            cs::Console::writeLine("Timer bad call, counts: ", counter);
            isFailed = true;
        }

        isChecked = true;
    });

    while (expectedCalls != counter && !isFailed) {
        timePoint = std::chrono::high_resolution_clock::now();
        timer.start(timeTickTime, cs::Timer::Type::HighPrecise);

        while (!isCalled && !isFailed);

        isCalled = false;
        timer.stop();

        cs::Console::writeLine("Wait Cycle iteration");
    }

    cs::Console::writeLine("Left main await cycle");

    while (!isChecked);

    ASSERT_EQ(expectedCalls, counter);
    ASSERT_EQ(isFailed, false);
}
