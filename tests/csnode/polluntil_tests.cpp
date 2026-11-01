#include "gtest/gtest.h"

#include <chrono>

#include <lib/system/polluntil.hpp>

using namespace std::chrono_literals;
using cs::pollUntil;

namespace {
auto never = [] { return false; };
}  // namespace

// the executor answers on the third attempt: no need to wait for the whole delay
TEST(PollUntil, StopsAtTheFirstSuccess) {
    int calls = 0;
    const auto start = std::chrono::steady_clock::now();
    EXPECT_TRUE(pollUntil(5000ms, 20ms, [&] { return ++calls == 3; }, never));
    EXPECT_EQ(calls, 3);
    EXPECT_LT(std::chrono::steady_clock::now() - start, 1000ms);
}

// never answers: tried until the deadline, the last attempt at or after it
TEST(PollUntil, LastAttemptAtTheDeadline) {
    int calls = 0;
    std::chrono::steady_clock::time_point last;
    const auto start = std::chrono::steady_clock::now();
    EXPECT_FALSE(pollUntil(250ms, 100ms, [&] { ++calls; last = std::chrono::steady_clock::now(); return false; }, never));
    EXPECT_GE(calls, 3);
    EXPECT_LE(calls, 6);
    EXPECT_GE(last - start, 250ms);
}

TEST(PollUntil, AStopEndsItEarly) {
    int calls = 0;
    EXPECT_FALSE(pollUntil(5000ms, 10ms, [&] { ++calls; return false; }, [&] { return calls >= 2; }));
    EXPECT_EQ(calls, 2);
}

TEST(PollUntil, NoDelayMeansOneAttempt) {
    int calls = 0;
    EXPECT_FALSE(pollUntil(0ms, 100ms, [&] { ++calls; return false; }, never));
    EXPECT_EQ(calls, 1);
    EXPECT_TRUE(pollUntil(0ms, 100ms, [] { return true; }, never));
}
