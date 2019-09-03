#include "observer.hpp"

#include <utility>

#include <client/config.hpp>

cs::config::Observer::Observer(Config& config, boost::program_options::variables_map& map)
: config_(config)
, map_(map) {
    thread_ = std::thread(&Observer::eventLoop, this);
}

cs::config::Observer::~Observer() {
    stop();
}

void cs::config::Observer::stop() {
    if (isObserved_.load(std::memory_order_acquire)) {
        isObserved_.store(true, std::memory_order_release);
        variable_.notify_one();

        thread_.join();
    }
}

bool cs::config::Observer::isObserved() const {
    return isObserved_.load(std::memory_order_acquire);
}

void cs::config::Observer::eventLoop() {
    isObserved_.store(true, std::memory_order_release);

    std::unique_lock lock(mutex_);

    while (isObserved_.load(std::memory_order_acquire)) {
        variable_.wait_for(lock, std::chrono::milliseconds(config_.observerWaitTime()));

        if (!isObserved_.load(std::memory_order_acquire)) {
            break;
        }

        Config config = Config::read(map_);

        if (config.isGood()) {
            if (config_ != config) {
                emit configChanged(config, config_);
                config_ = std::move(config);
            }
        }
        else {
            cswarning() << "config.ini can not be read by observer, check config.ini format or file existing";
        }
    }
}
