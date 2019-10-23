#include "csnode/configholder.hpp"

cs::ConfigHolder& cs::ConfigHolder::instance() {
    static ConfigHolder holder;
    return holder;
}

void cs::ConfigHolder::onConfigChanged(const Config& config) {
    setConfig(config);
}

void cs::ConfigHolder::setConfig(const Config& config) {
    config_.exchange(config);
}

const Config* cs::ConfigHolder::config() const {
    return config_.operator->();
}

cs::ConfigHolder::ConfigHolder() = default;
cs::ConfigHolder::~ConfigHolder() = default;
