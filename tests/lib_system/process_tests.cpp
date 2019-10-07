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
