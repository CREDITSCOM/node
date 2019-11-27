#ifndef EXECUTORMANAGER_HPP
#define EXECUTORMANAGER_HPP

#include <string>
#include <vector>
#include <optional>

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif

#include <boost/process.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace cs {
class ExecutorManager {
public:
    using ProcessId = boost::process::pid_t;

    // returns true if executor exists at java processes
    bool isExecutorProcessRunning(ProcessId id) const;
    bool isExecutorProcessRunning() const;

    // returns all executor pids if executor exists at java processes
    std::optional<std::vector<ProcessId>> executorProcessIds() const;

    // stops executor
    bool stopExecutorProcess(ProcessId id);

    // stops all executor processes
    bool stopExecutorProcess();

private:
    std::string jpsData() const;
    void terminate(boost::process::pid_t pid);

    const std::string jpsName_ = "jps";
    const std::string executorName_ = "contract-executor";
};
}

#endif // EXECUTORMANAGER_HPP
