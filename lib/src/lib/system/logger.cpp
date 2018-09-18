#include <lib/system/logger.hpp>
#include <chrono>
#include <fstream>
#include <vector>
#include <iomanip>
#include <thread>
#include <atomic>
#include <mutex>
#include <algorithm>

extern thread_local bool trace = true;

struct cs::Logger::Impl
{
    std::thread thread;
    std::vector<std::string> info;
    std::atomic<bool> isTerminateRequest;
    std::mutex mutex;
    std::chrono::milliseconds sleepTime = std::chrono::milliseconds(10);

    //main thread loop
    void loop();

    Impl();
    ~Impl();
};

cs::Logger::Impl::Impl()
{
    isTerminateRequest = false;
    thread = std::thread(&Impl::loop, this);
}

cs::Logger::Impl::~Impl()
{
    isTerminateRequest = true;

    if (thread.joinable())
        thread.join();

    info.clear();
}

template<typename Buffer>
static void writeToFile(const Buffer& buffer);

void cs::Logger::Impl::loop()
{
    while (!isTerminateRequest)
    {
        mutex.lock();

        if (!info.empty())
        {
            std::vector<std::string> tempBuffer(info.size());
            std::move(info.begin(), info.end(), tempBuffer.begin());
            info.clear();

            mutex.unlock();

            writeToFile(tempBuffer);
        }
        else
            mutex.unlock();

        std::this_thread::sleep_for(sleepTime);
    }
}

template<typename Buffer>
static void writeToFile(const Buffer& buffer)
{
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);

    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%Y_%m_%d");

    std::ofstream file("log/" + ss.str() + "_log.txt", std::ios_base::app);
    ss.str(std::string());

    std::for_each(buffer.begin(), buffer.end(), [&](const std::string& str) {
        ss << std::put_time(std::localtime(&in_time_t), "%Hh %Mm %Ss:");
        ss << str + "\n\n";
    });

    file << ss.rdbuf();
    file.close();
}

cs::Logger::Logger() :
    mPimpl(new Impl())
{
}

const cs::Logger& cs::Logger::instance()
{
    static const cs::Logger logger;
    return logger;
}

void cs::Logger::toFile(const std::string& log) const noexcept
{
    std::lock_guard<std::mutex> lock(mPimpl->mutex);
    mPimpl->info.push_back(log);
}

void cs::Logger::toFile(const std::stringstream& log) const noexcept
{
    toFile(log.rdbuf()->str());
}

const std::string cs::InlineLoggerStatic::error = "[ERROR] ";
const std::string cs::InlineLoggerStatic::warning = "[WARNING] ";
const std::string cs::InlineLoggerStatic::info = "[INFO] ";
