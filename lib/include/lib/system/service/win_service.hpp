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

    static DWORD __stdcall onExtendedServiceControlEventAdapter(
        DWORD code,
        DWORD type,
        LPVOID data,
        LPVOID context
    ) {
        auto this_ = reinterpret_cast<Service*>(context);
        this_->onExtendedServiceControlEvent(code, type, data);
    }

    DWORD onExtendedServiceControlEvent(DWORD code, DWORD type, LPVOID data);
    DWORD onEvent(DWORD code);

    bool setStatus(
        DWORD id,
        DWORD ecode = 0,
        DWORD speccode = 0,
        DWORD checkPoint = 0,
        DWORD hint = 0
    );

    void start(DWORD ac, LPSTR* av);

    ServiceOwner& owner_;
    const char* serviceName_;
    WinEvent event_;
    SERVICE_STATUS status_;
    SERVICE_STATUS_HANDLE statusHandler_;
};

inline Service::Service(ServiceOwner& owner, const char* serviceName)
    : owner_(owner), serviceName_(serviceName), statusHandler_(nullptr) {}

inline bool Service::run() {
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

inline Service*& Service::instance() {
    static Service* ptr = nullptr;
    return ptr;
}

inline void Service::start(DWORD ac, LPSTR* av) {
    status_.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    status_.dwControlsAccepted = SERVICE_ACCEPT_STOP
                               | SERVICE_ACCEPT_SHUTDOWN
#if _WIN32_WINNT >= _WIN32_WINNT_VISTA
                               | SERVICE_ACCEPT_PRESHUTDOWN
#endif // _WIN32_WINNT >= WIN32_WINNT_VISTA
                               | SERVICE_ACCEPT_PAUSE_CONTINUE
#if _WIN32_WINNT >= _WIN32_WINNT_WINXP
                               | SERVICE_ACCEPT_SESSIONCHANGE
#endif // _WIN32_WINNT >= WIN32_WINNT_WINXP
                               | SERVICE_ACCEPT_PARAMCHANGE;
    status_.dwWin32ExitCode = NO_ERROR;
    status_.dwServiceSpecificExitCode = NO_ERROR;
    status_.dwCheckPoint = 0;
    status_.dwWaitHint = 0;

    event_.makeSignaled();

    statusHandler_ = RegisterServiceCtrlHandlerExA(
        serviceName_,
        &Service::onExtendedServiceControlEventAdapter,
        this
    );

    int errorCode = 0;

    if (!statusHandler_) {
        errorCode = int(GetLastError());
    }
    else {
        try {
            errorCode = owner_.onInit(serviceName_) ? 0 : -1;
            if (errorCode == 0) {
                setStatus(SERVICE_RUNNING);
                errorCode = owner_.onRun(serviceName_) ? 0 : -1;
                event_.wait();
            }
        }
        catch (...) {
            bool ok = owner_.onException();
            if (errorCode == 0) {
                errorCode = ok ? 0 : -1;
            }
        }
    }

    this->setStatus(SERVICE_STOPPED, errorCode, status_.dwServiceSpecificExitCode);
}

inline DWORD Service::onExtendedServiceControlEvent(DWORD code, DWORD type, LPVOID data) {

}

inline DWORD Service::onEvent(DWORD code) {

}

inline bool Service::setStatus(
    DWORD id,
    DWORD ecode,
    DWORD speccode,
    DWORD checkPoint,
    DWORD hint
) {
    status_.dwCurrentState = id;
    status_.dwWin32ExitCode = (speccode != 0 ? ERROR_SERVICE_SPECIFIC_ERROR : ecode);
    status_.dwServiceSpecificExitCode = speccode;
    status_.dwCheckPoint = checkPoint;
    status_.dwWaitHint = hint;

    return SetServiceStatus(statusHandler_, &status_);
}

} // namespace cs
