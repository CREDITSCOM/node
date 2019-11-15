#ifndef CONFIGHOLDER_HPP
#define CONFIGHOLDER_HPP

#include <config.hpp>

#include <lib/system/signals.hpp>
#include <lib/system/lockfreechanger.hpp>

class Config;

namespace cs {
class ConfigHolder {
    ConfigHolder();
    ~ConfigHolder();

public:
    static ConfigHolder& instance();

    void setConfig(const Config& config);
    const Config* config() const;

public slots:
    void onConfigChanged(const Config& config);

private:
    cs::LockFreeChanger<Config> config_;
};
} // namespace cs
#endif  // CONFIGHOLDER_HPP
