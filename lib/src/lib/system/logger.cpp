#include <lib/system/logger.hpp>
#include <chrono>
#include <fstream>
#include <vector>
#include <iomanip>
#include <thread>
#include <atomic>
#include <mutex>
#include <algorithm>

#ifdef __cpp_lib_filesystem 
    #define STD_FILE_SYSTEM
    #include <filesystem>
#endif

extern thread_local bool trace = true;

template<typename Buffer>
static void writeToFile(const Buffer& buffer);

const static std::string fileLogPath = "log";

struct cs::Logger::Impl
{
    std::thread thread;
    std::vector<std::string> info;
    std::atomic<bool> isTerminateRequest;
    std::atomic<bool> isEmpty;
    std::mutex mutex;
    std::chrono::milliseconds sleepTime = std::chrono::milliseconds(100);

    //main thread loop
    void loop();

    Impl();
    ~Impl();
};

cs::Logger::Impl::Impl()
{
    isTerminateRequest = false;
    isEmpty = true;

#ifdef STD_FILE_SYSTEM
    std::filesystem::path path = std::filesystem::current_path().string() +
        "/" + fileLogPath;

    if (!std::filesystem::exists(path)) {
        std::filesystem::create_directory(path);
    }

#endif
    thread = std::thread(&Impl::loop, this);

    csdebug() << "File logger created";
}

cs::Logger::Impl::~Impl()
{
    isTerminateRequest = true;
    isEmpty = true;

    if (thread.joinable())
        thread.join();

    if (!info.empty())
        writeToFile(info);

    info.clear();

    csdebug() << "File logger destroyed";
}

void cs::Logger::Impl::loop()
{
    while (!isTerminateRequest)
    {
        if (!isEmpty)
        {
            std::vector<std::string> buffer;

            {
                std::lock_guard<std::mutex> lock(mutex);

                std::vector<std::string> tempBuffer(info.size());
                std::move(info.begin(), info.end(), tempBuffer.begin());

                info.clear();
                isEmpty = true;

                buffer = std::move(tempBuffer);
            }

            if (!buffer.empty())
                writeToFile(buffer);
        }

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

    std::ofstream file(fileLogPath + "/" + ss.str() + "_log.txt", std::ios_base::app);
    ss.str(std::string());

    std::for_each(buffer.begin(), buffer.end(), [&](const std::string& str) {
        ss << std::put_time(std::localtime(&in_time_t), "%Hh %Mm %Ss:");
        ss << str << "\n";
    });

    file << ss.rdbuf();
    file.close();
}

cs::Logger::Logger():
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
    if (log.empty())
        return;

    std::lock_guard<std::mutex> lock(mPimpl->mutex);

    if (mPimpl->info.empty())
        mPimpl->isEmpty = false;

    mPimpl->info.push_back(log);
}

void cs::Logger::toFile(const std::stringstream& log) const noexcept
{
    toFile(log.rdbuf()->str());
}

const std::string cs::InlineLoggerStatic::error = "[ERROR] ";
const std::string cs::InlineLoggerStatic::warning = "[WARNING] ";
const std::string cs::InlineLoggerStatic::info = "[INFO] ";
