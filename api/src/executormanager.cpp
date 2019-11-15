#include <executormanager.hpp>

#include <regex>

#include <lib/system/process.hpp>

bool cs::ExecutorManager::isExecutorProcessRunning() const {
    return jpsData().find(executorName_) != std::string::npos;
}

std::optional<boost::process::pid_t> cs::ExecutorManager::executorProcessPid() const {
    auto data = jpsData();
    auto pos = data.find(executorName_);

    if (pos == std::string::npos) {
        return std::nullopt;
    }

    std::regex regexpr("([0-9]*)" + executorName_);
    std::smatch match;

    if (!std::regex_search(data, match, regexpr)) {
        return std::nullopt;
    }

    try {
        auto value = std::stoi(match[1]);
        return std::make_optional(value);
    }
    catch (const std::exception&) {
        return std::nullopt;
    }
}

bool cs::ExecutorManager::stopExecutorProcess() {
    auto pid = executorProcessPid();

    if (!pid) {
        return false;
    }

    cs::Process process(pid.value());
    process.launch(cs::Process::Options::Attach);
    process.terminate();

    return !isExecutorProcessRunning();
}

std::string cs::ExecutorManager::jpsData() const {
    std::atomic<bool> finished = false;
    cs::Process process(jpsName_);

    cs::Connector::connect(&process.finished, [&](auto...) {
        finished.store(true, std::memory_order_release);
    });

    cs::Connector::connect(&process.errorOccured, [&](auto...) {
        finished.store(true, std::memory_order_release);
    });

    process.launch(cs::Process::Options::OutToStream);

    while (!finished.load(std::memory_order_acquire));

    std::string result;

    while (process.out().good()) {
        std::string data;
        process.out() >> data;

        result += data;
    }

    return result;
}
