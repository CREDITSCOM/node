#include "gtest/gtest.h"

#include <lib/system/process.hpp>
#include <lib/system/fileutils.hpp>
#include <lib/system/console.hpp>

#ifdef WIN32
static const char* extension = ".exe";
#else
static const char* extension = "";
#endif

static const char* programName = "testprogram";
static const std::string& programPath = std::string("../testprogram/") + programName + extension;

static bool isProgramExists() {
    static bool status = cs::FileUtils::isFileExists(programPath);
    return status;
}

#define PROCESS_TEST_START() if (!isProgramExists()) return;

TEST(Process, BaseUsage) {
    PROCESS_TEST_START()

    cs::Process process(programPath);

    std::atomic<bool> isStarted = false;
    std::atomic<bool> isTerminated = false;

    cs::Connector::connect(&process.started, [&]{
        cs::Console::writeLine(programName, " started");
        isStarted = true;
    });

    cs::Connector::connect(&process.finished, [&](int, const std::system_error&) {
        cs::Console::writeLine(programName, " finished");
        isTerminated = true;
    });

    process.launch();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    ASSERT_TRUE(isStarted);

    process.terminate();

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    ASSERT_TRUE(isTerminated);
}

TEST(Process, DISABLED_Termination) {
    PROCESS_TEST_START()

    size_t maxTermination = 30000;
    cs::Process process(programPath + std::to_string(maxTermination));
    std::atomic<bool> isTerminated = false;

    cs::Connector::connect(&process.finished, [&](int, const std::system_error&) {
        cs::Console::writeLine(programName, " finished");
        isTerminated = true;
    });

    cs::Connector::connect(&process.errorOccured, [&](const cs::ProcessException&) {
        isTerminated = true;
    });

    process.launch();
    while(!process.isRunning());

    process.terminate();
    std::this_thread::sleep_for(std::chrono::milliseconds(maxTermination/2));

    ASSERT_TRUE(isTerminated);
}

TEST(Process, HighLoadUsage) {
    PROCESS_TEST_START()

    const size_t maxLaunchCount = 250;
    cs::Process process(programPath);

    std::atomic<size_t> launchCount = { 0 };
    std::atomic<size_t> finishCount = { 0 };

    cs::Connector::connect(&process.started, [&]{
        launchCount.fetch_add(1u, std::memory_order_release);
        cs::Console::writeLine("Launch count ", launchCount.load(std::memory_order_acquire));
    });

    cs::Connector::connect(&process.finished, [&](int, const std::system_error&) {
        finishCount.fetch_add(1u, std::memory_order_release);
        cs::Console::writeLine("Finish count ", finishCount.load(std::memory_order_acquire));
    });

    process.launch();

    // the child runs only ~10 ms: it may already be gone when we first look
    while (!process.isRunning() && finishCount.load(std::memory_order_acquire) == 0);
    size_t launches = 1;

    while (true) {
        if (maxLaunchCount <= launches) {
            break;
        }

        if (!process.isRunning()) {
            process.launch();
            ++launches;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // the last `finished` can be emitted before we get here, so wait on the counter, not on a flag
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (finishCount.load(std::memory_order_acquire) < maxLaunchCount && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    cs::Console::writeLine("Launch count ", launchCount.load(std::memory_order_acquire));
    cs::Console::writeLine("Finish count ", finishCount.load(std::memory_order_acquire));

    ASSERT_EQ(launchCount.load(std::memory_order_acquire), maxLaunchCount);
    ASSERT_EQ(finishCount.load(std::memory_order_acquire), maxLaunchCount);
}
