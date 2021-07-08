#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif // !NOMINMAX

#include <windows.h>

#include "service_owner.hpp"

namespace cs {

class WinEvent {
public:
    WinEvent(bool initState = false /* initial state is nonsignaled */)
        : eventHandler_(CreateEvent(0, true, initState, 0)) {}

    ~WinEvent() {
        if (eventHandler_ != nullptr) {
            CloseHandle(eventHandler_);
        }
    }

    DWORD lastError() {
        return lastError_;
    }

    bool makeNonSignaled() {
        if (eventHandler_ == nullptr) {
            lastError_ = -1;
            return false;
        }

        if (!ResetEvent(eventHandler_)) {
            lastError_ = GetLastError();
            return false;
        }

        return true;
    }

    bool makeSignaled() {
        if (eventHandler_ == nullptr) {
            lastError_ = -1;
            return false;
        }

        if (!SetEvent(eventHandler_)) {
            lastError_ =  GetLastError();
            return false;
        }

        return true;
    }

    bool wait() {
        if (eventHandler_ == nullptr) {
            lastError_ = -1;
            return false;
        }

        if (WaitForSingleObject(eventHandler_, INFINITE) != WAIT_OBJECT_0) {
            lastError_ = GetLastError();
            return false;
        }

        return true;
    }

private:
    HANDLE eventHandler_;
    DWORD lastError_ = 0;
};

class Service {
public:
    Service(ServiceOwner&, const char* serviceName = nullptr);

    bool run();

private:
    ServiceOwner& owner_;
    const char* serviceName_;
};

Service::Service(ServiceOwner& owner, const char* serviceName)
    : owner_(owner), serviceName_(serviceName) {}

bool Service::run() {
    return true;
}

} // namespace cs
