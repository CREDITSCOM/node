#include <string>
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
    // returns true if executor exists at java processes
    bool isExecutorProcessRunning() const;

    // returns executor pid if executor exists at java processes
    std::optional<boost::process::pid_t> executorProcessPid() const;

    // stops executor, returns false if executor does not exist
    bool stopExecutorProcess();

private:
    std::string jpsData() const;

    const std::string jpsName_ = "jps";
    const std::string executorName_ = "contract-executor";
};
}
