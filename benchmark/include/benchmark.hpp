#ifndef BENCHMARK_HPP
#define BENCHMARK_HPP

#include <chrono>
#include <future>
#include <memory>

#include <lib/system/console.hpp>

#include <timeoutexception.hpp>

#define forever for(;;)

namespace cs {
class Benchmark {
public:
    using Duration = decltype(std::chrono::steady_clock::now() - std::chrono::steady_clock::now());
    using Future = std::future<std::pair<Duration, bool>>;
    using FuturePtr = std::shared_ptr<Future>;

    template <typename Func>
    static bool run(Func f, std::chrono::seconds max = std::chrono::seconds(10)) {
        cs::Console::writeLine("\nStart benchmark");

        auto lambda = [=] {
            auto now = std::chrono::steady_clock::now();
            auto result = f();

            auto difference = std::chrono::steady_clock::now() - now;
            return std::make_pair(difference, result);
        };

        future_ = std::make_shared<Future>(std::async(std::launch::async, lambda));

        auto now = std::chrono::steady_clock::now();
        std::chrono::milliseconds ms(0);

        forever {
            static const std::chrono::milliseconds waitTime(10);
            std::future_status status = future_->wait_for(waitTime);

            if (status == std::future_status::ready) {
                break;
            }
            else {
                auto point = std::chrono::steady_clock::now();
                ms += std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - now);
                now = point;

                if (ms >= max) {
                    ms = std::chrono::milliseconds(0);
                    throw TimeOutException();
                }
            }
        }

        const auto [difference, result] = future_->get();
        static const char* title = "Benchmark finished, time in";
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(difference);

        if (duration.count() != 0) {
            cs::Console::writeLine(title, " ms ", duration.count());
        }
        else {
            cs::Console::writeLine(title, " ns ", std::chrono::duration_cast<std::chrono::nanoseconds>(difference).count());
        }

        future_ = FuturePtr();
        return result;
    }

private:
    inline static FuturePtr future_;
};

}  // namespace cs

#endif // BENCHMARK_HPP
