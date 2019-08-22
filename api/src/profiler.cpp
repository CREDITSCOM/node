#include <profiler/profiler.hpp>

#include <iostream>

#include <lib/system/utils.hpp>
#include <lib/system/fileutils.hpp>

cs::ProfilerFileLogger::~ProfilerFileLogger() {
    stop();
}

void cs::ProfilerFileLogger::stop() {
    if (isRunning()) {
        isRunning_.store(false, std::memory_order_release);
        variable_.notify_all();

        thread_.join();
    }
}

void cs::ProfilerFileLogger::start() {
    if (!isRunning()) {
        isRunning_.store(true, std::memory_order_release);
        thread_ = std::thread(&cs::ProfilerFileLogger::eventLoop, this);
    }
}

bool cs::ProfilerFileLogger::isRunning() const {
    return isRunning_.load(std::memory_order_acquire);
}

void cs::ProfilerFileLogger::add(const std::string& message) {
    auto time = cs::Utils::formattedCurrentTime(cs::Utils::TimeFormat::DefaultMs);
    Data data { message, std::move(time) };

    while (!queue_.push(data));
    variable_.notify_one();
}

cs::ProfilerFileLogger::ProfilerFileLogger(size_t size)
: fileName_(path + "/" + fileName)
, buffer_(size)
, queue_(size) {
    if (!cs::FileUtils::createPathIfNoExist(path)) {
        std::cout << "Profiler file logger path creation failed\n";
    }

    start();
}

void cs::ProfilerFileLogger::eventLoop() {
    while (isRunning_.load(std::memory_order_acquire)) {
        std::unique_lock lock(mutex_);
        variable_.wait(lock, [this] {
            return queue_.read_available() || !isRunning();
        });

        while (queue_.read_available() != 0) {
            Data data;
            while (!queue_.pop(data));

            buffer_.push_back(std::move(data));
        }

        std::ofstream file(fileName_, std::ofstream::trunc);

        for (auto iter = buffer_.begin(); iter != buffer_.end(); ++iter) {
            auto length = static_cast<size_t>(std::distance(iter, buffer_.end()));
            auto index = buffer_.size() - length;

            const Data& data = *iter;
            file << formatter(data.message, data.time, index) << "\n";
        }
    }
}
