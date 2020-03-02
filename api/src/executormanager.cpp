#include <executormanager.hpp>

#include <regex>

#include <lib/system/process.hpp>
#include <lib/system/concurrent.hpp>

#include <csnode/configholder.hpp>

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
    std::atomic<bool> errorOccured = false;
    cs::Process process(cs::ConfigHolder::instance().config()->getApiSettings().jpsCmdLine);

    cs::Connector::connect(&process.finished, [&](auto...) {
        finished.store(true, std::memory_order_release);
    });

    cs::Connector::connect(&process.errorOccured, [&](const auto& e) {
        csdebug() << "Executor manager, run jps failed: " << e.what();
        errorOccured.store(true, std::memory_order_release);
    });

    process.launch(cs::Process::Options::OutToStream);

    while (!finished.load(std::memory_order_acquire) &&
           !errorOccured.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }

    cs::Connector::disconnect(&process.finished);
    cs::Connector::disconnect(&process.errorOccured);

    if (!process.isPipeValid()) {
        return std::string{};
    }

    return process.out();
}

void cs::ExecutorManager::terminate(boost::process::pid_t pid) {
#ifdef __linux__
    std::shared_ptr<cs::Process> process = std::make_shared<cs::Process>("kill -9 " + std::to_string(pid));
#else
    std::shared_ptr<cs::Process> process = std::make_shared<cs::Process>("taskkill /PID " + std::to_string(pid) + " /F");
#endif
    cs::Connector::connect(&process->errorOccured, [=](const auto& exeception) {
        cs::Concurrent::run([storage = process] {
            storage->terminate();
        });

        cserror() << "Executor manager terminate by pid error " << exeception.what();
    });

    process->launch();
    process->wait();
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
