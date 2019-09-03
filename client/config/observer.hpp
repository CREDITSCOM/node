#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <condition_variable>

#include <boost/program_options.hpp>

#include <lib/system/signals.hpp>

class Config;

namespace cs::config {
using ConfigChangeSignal = cs::Signal<void(const Config& newConfig, const Config& previousConfig)>;

class Observer {
public:
    explicit Observer(Config& config, boost::program_options::variables_map& map);
    ~Observer();

    void stop();
    bool isObserved() const;

public signals:
    // generates when new config updated
    // called from another thread
    ConfigChangeSignal configChanged;

private:
    void eventLoop();

    Config& config_;
    boost::program_options::variables_map& map_;

    std::atomic<bool> isObserved_ = { false };
    std::mutex mutex_;
    std::condition_variable variable_;
    std::thread thread_;
};
}
