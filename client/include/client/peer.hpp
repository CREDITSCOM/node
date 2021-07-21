#pragma once

#include <memory>

#include <lib/system/service/service.hpp>
#include <csnode/node.hpp>

namespace cs {

class Peer : public ServiceOwner {
public:
    Peer(const char* serviceName);
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
};

} // namespace cs
