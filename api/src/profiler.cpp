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
    while (!queue_.push(cs::Utils::formattedCurrentTime() + std::string(", ") + message));
    variable_.notify_one();
}

void cs::ProfilerFileLogger::add(const std::string& message, size_t time) {
    add(message + std::to_string(time));
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
    while (isRunning_) {
        std::unique_lock lock(mutex_);
        variable_.wait(lock, [this] {
            return queue_.read_available() || !isRunning();
        });

        while (queue_.read_available() != 0) {
            std::string message;
            while (!queue_.pop(message));

            buffer_.push_back(std::move(message));
        }

        std::ofstream file(fileName_, std::ofstream::trunc);

        for (auto iter = buffer_.begin(); iter != buffer_.end(); ++iter) {
            auto length = static_cast<size_t>(std::distance(iter, buffer_.end()));
            auto index = buffer_.size() - length;

            file << formatter(*iter, index) << "\n";
        }
    }
}
