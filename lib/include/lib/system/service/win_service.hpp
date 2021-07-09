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
    Service(ServiceOwner&, const char* serviceName);

    bool run();

private:
    static Service*& instance();
    static void __stdcall serviceMain(DWORD ac, LPSTR* av) {
        instance()->start(ac, av);
    }

    void start(DWORD ac, LPSTR* av);

    ServiceOwner& owner_;
    const char* serviceName_;
    WinEvent event_;
    SERVICE_STATUS status_;
    SERVICE_STATUS_HANDLE statusHandler_;
};

Service::Service(ServiceOwner& owner, const char* serviceName)
    : owner_(owner), serviceName_(serviceName), statusHandler_(nullptr) {}

bool Service::run() {
    if (serviceName_ == nullptr) {
        return false;
    }
#ifdef DISABLE_DAEMON
    try {
        if (!owner_.onInit(serviceName_)) {
            return false;
        }
        if (!owner_.onRun(serviceName_)) {
            return false;
        }
    }
    catch (...) {
        return owner_.onException();
    }
    return true;
#endif // DISABLE_DAEMON
    SERVICE_TABLE_ENTRYA serviceTable;
    serviceTable.lpServiceName = const_cast<char*>(serviceName_);
    serviceTable.lpServiceProc = &Service::serviceMain;
    instance() = this;
    bool result = StartServiceCtrlDispatcherA(&serviceTable);
    instance() = nullptr;
    return result;
}

Service*& Service::instance() {
    static Service* ptr = nullptr;
    return ptr;
}

void Service::start(DWORD ac, LPSTR* av) {

}

} // namespace cs
