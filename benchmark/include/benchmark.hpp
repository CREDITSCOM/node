#ifndef BENCHMARK_HPP
#define BENCHMARK_HPP

#include <chrono>
#include <future>
#include <memory>

#include <lib/system/console.hpp>

#include <timeoutexception.hpp>

#define forever for(;;)

namespace cs {
class BenchmarkBase {
protected:
    using Duration = decltype(std::chrono::steady_clock::now() - std::chrono::steady_clock::now());

    static void started() {
        cs::Console::writeLine("\nStart benchmark");
    }

    template<typename T, typename Future>
    static void finished(const T& difference, Future& future) {
        static const char* title = "Benchmark finished, time in";
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(difference);

        if (duration.count() != 0) {
            cs::Console::writeLine(title, " ms ", duration.count());
        }
        else {
            cs::Console::writeLine(title, " ns ", std::chrono::duration_cast<std::chrono::nanoseconds>(difference).count());
        }

        future = Future{};
    }

    template <typename T, typename K>
    static void monitor(std::shared_ptr<T> future, const K& max) {
        auto now = std::chrono::steady_clock::now();
        std::chrono::milliseconds ms(0);

        forever {
            static const std::chrono::milliseconds waitTime(10);
            std::future_status status = future->wait_for(waitTime);

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
    }
};

template<typename T>
class Benchmark : public BenchmarkBase {
    using Duration = typename BenchmarkBase::Duration;
    using Future = std::future<std::pair<Duration, T>>;
    using FuturePtr = std::shared_ptr<Future>;

public:
    template <typename Func>
    static T run(Func f, std::chrono::seconds max = std::chrono::seconds(10)) {
        BenchmarkBase::started();

        auto lambda = [=] {
            auto now = std::chrono::steady_clock::now();
            auto result = f();

            auto difference = std::chrono::steady_clock::now() - now;
            return std::make_pair(difference, result);
        };

        future_ = std::make_shared<Future>(std::async(std::launch::async, lambda));
        BenchmarkBase::monitor(future_, max);

        const auto [difference, result] = future_->get();
        BenchmarkBase::finished(difference, future_);

        return result;
    }

private:
    inline static FuturePtr future_;
};

template <>
class Benchmark<void> : public BenchmarkBase {
    using Duration = typename BenchmarkBase::Duration;
    using Future = std::future<Duration>;
    using FuturePtr = std::shared_ptr<Future>;

public:
    template <typename Func>
    static void run(Func f, std::chrono::seconds max = std::chrono::seconds(10)) {
        static_assert (std::is_same_v<std::invoke_result_t<Func>, void>, "Benchmark<void> should use only functors with void result type");

        BenchmarkBase::started();

        auto lambda = [=] {
            auto now = std::chrono::steady_clock::now();
            f();
            return std::chrono::steady_clock::now() - now;
        };

        future_ = std::make_shared<Future>(std::async(std::launch::async, lambda));
        BenchmarkBase::monitor(future_, max);

        const auto difference = future_->get();
        BenchmarkBase::finished(difference, future_);
    }

private:
    inline static FuturePtr future_;
};

}  // namespace cs

#endif // BENCHMARK_HPP
