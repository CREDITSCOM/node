#pragma once

#include <memory>

#include <lib/system/service/service.hpp>
#include <csnode/node.hpp>
#include <observer.hpp>

namespace cs {

class Peer : public ServiceOwner {
public:
    Peer(const char* serviceName, config::Observer&, bool onlyInit);
    int executeProtocol();

protected:
    // ServiceOwner interface
    bool onInit(const char* serviceName) override;
    bool onRun(const char* serviceName) override;
    bool onStop() override;
#ifndef _WIN32
    bool onFork(const char* serviceName, pid_t) override;
#endif // !_WIN32

private:
    Service service_;
    std::unique_ptr<Node> node_;
    config::Observer& observer_;
    const bool onlyInit_;
};

} // namespace cs
