#ifndef PROFILER_HPP
#define PROFILER_HPP

#include <vector>
#include <string>
#include <fstream>
#include <functional>

#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif

#include <boost/lockfree/spsc_queue.hpp>
#include <boost/circular_buffer.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace cs {
// you can use user defined formatter, args are:
// message - logger buffer message,
// time - formatted time stamp,
// index - index of message at buffer
using ProfilerFileLoggerFormatter = std::function<std::string(const std::string& message, const std::string& time, size_t index)>;

// logs messages to file
class ProfilerFileLogger {
    struct Data {
        std::string message;
        std::string time;
    };

public:
    enum Options {
        DefaultBufferSize = 100
    };

    ~ProfilerFileLogger();

    static ProfilerFileLogger& instance() {
        static ProfilerFileLogger logger(ProfilerFileLogger::bufferSize);
        return logger;
    }

    // settings, setup them before logger instance
    inline static size_t bufferSize = Options::DefaultBufferSize;
    inline static std::string fileName = "profiler.txt";
    inline static std::string path = "profiler";
    inline static ProfilerFileLoggerFormatter formatter = [](const std::string& message, const std::string& time, size_t) {
        return time + std::string(" ") + message;
    };

    void stop();
    void start();
    bool isRunning() const;

    void add(const std::string& message);

    template<typename TimePoint, typename Result = decltype(std::declval<std::chrono::steady_clock::time_point>() - std::declval<std::chrono::steady_clock::time_point>()),
             typename = std::enable_if_t<std::is_same_v<TimePoint, Result>>>
    void add(const std::string& message, const TimePoint& point) {
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(point);
        auto format = message + ", ";

        if (duration.count() != 0) {
            add(format + std::to_string(duration.count()) + " ms");
        }
        else {
            add(format + std::to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(point).count()) + " ns");
        }
    }

protected:
    explicit ProfilerFileLogger(size_t size);
    void eventLoop();

private:
    std::string fileName_;

    boost::circular_buffer<Data> buffer_;
    boost::lockfree::spsc_queue<Data> queue_;

    std::mutex mutex_;
    std::condition_variable variable_;
    std::thread thread_;

    std::atomic<bool> isRunning_ = { false };
};

// RAII watcher to file logger
class Profiler {
public:
    explicit Profiler(const std::string& message)
    : message_(message)
    , point_(std::chrono::steady_clock::now()) {
    }

    ~Profiler() {
        auto point = std::chrono::steady_clock::now() - point_;
        cs::ProfilerFileLogger::instance().add(message_, point);
    }

private:
    std::string message_;
    std::chrono::steady_clock::time_point point_;
};
}

#endif // PROFILER_HPP
