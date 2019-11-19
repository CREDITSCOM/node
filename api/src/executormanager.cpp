#include <executormanager.hpp>

#include <regex>

#include <lib/system/process.hpp>

bool cs::ExecutorManager::isExecutorProcessRunning(ProcessId id) const {
    return jpsData().find(std::to_string(id) + executorName_) != std::string::npos;
}

bool cs::ExecutorManager::isExecutorProcessRunning() const {
    return executorProcessIds().has_value();
}

bool cs::ExecutorManager::stopExecutorProcess(ProcessId id) {
    if (isExecutorProcessRunning(id)) {
        terminate(id);
    }

    return !isExecutorProcessRunning(id);
}

bool cs::ExecutorManager::stopExecutorProcess() {
    auto pids = executorProcessIds();

    if (!pids.has_value()) {
        return true;
    }

    for (auto pid : std::move(pids).value()) {
        terminate(pid);
    }

    return isExecutorProcessRunning();
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

void cs::ExecutorManager::terminate(boost::process::pid_t pid) {
    cs::Process process(pid);
    process.launch(cs::Process::Options::Attach);
    process.terminate();
}

std::optional<std::vector<cs::ExecutorManager::ProcessId>> cs::ExecutorManager::executorProcessIds() const {
    auto data = jpsData();
    auto pos = data.find(executorName_);

    if (pos == std::string::npos) {
        return std::nullopt;
    }

    std::regex regexpr("([0-9]*)" + executorName_);

    auto begin = std::sregex_iterator(data.begin(), data.end(), regexpr);
    auto end = std::sregex_iterator();

    if (!std::distance(begin, end)) {
        return std::nullopt;
    }

    std::vector<ProcessId> results;

    for (; begin != end; ++begin) {
        auto match = *begin;
        results.push_back(std::stoi(match.str()));
    }

    return std::make_optional(std::move(results));
}
