#include <peer.hpp>

namespace cs {

Peer::Peer(const char* serviceName)
    : service_(static_cast<ServiceOwner&>(*this), serviceName) {}

int Peer::executeProtocol() {
    return 0;
}

bool Peer::onInit(const char*) {
    return true;
}

bool Peer::onRun(const char*) {
    return true;
}

bool Peer::onStop() {
    return true;
}

#ifndef _WIN32
bool Peer::onFork(const char*, pid_t) {
    return true;
}
#endif // !_WIN32

} // namespace cs
