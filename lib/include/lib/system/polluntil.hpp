#ifndef POLLUNTIL_HPP
#define POLLUNTIL_HPP

#include <algorithm>
#include <chrono>
#include <thread>

namespace cs {

// Calls probe() now and then every step until it returns true, the time is up or stop() returns true.
// The last call is made at or after the deadline, so a probe that succeeds only at the very end still
// counts. Returns the result of the last call (false if stop() ended it before any success).
template <typename Probe, typename Stop>
bool pollUntil(std::chrono::milliseconds total, std::chrono::milliseconds step, Probe probe, Stop stop) {
    const auto deadline = std::chrono::steady_clock::now() + total;
    for (;;) {
        if (stop()) {
            return false;
        }
        if (probe()) {
            return true;
        }
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            return false;
        }
        // rounded up: rounded down, the last millisecond would spin with zero-length sleeps
        std::this_thread::sleep_for(std::min(step, std::chrono::ceil<std::chrono::milliseconds>(deadline - now)));
    }
}

}  // namespace cs

#endif  // POLLUNTIL_HPP
