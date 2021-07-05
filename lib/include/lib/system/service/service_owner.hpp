#pragma once

#ifndef _WIN32
#include <unistd.h>
#endif // !_WIN32

namespace cs {

class ServiceOwner {
public:
    virtual bool onInit(const char*) { return true; }
    virtual bool onRun(const char*) = 0;
    virtual bool onStop() = 0;
#ifndef _WIN32
    virtual bool onFork(const char*, pid_t) { return true; }
#endif // !_WIN32
    virtual bool onPause() { return true; }
    virtual bool onContinue() { return true; }
    virtual bool onParamChange() { return true; }
    virtual bool onException() { return true; }
#ifdef WIN32
#if (_WIN32_WINNT >= _WIN32_WINNT_WINXP)
    virtual bool onSessionChanged(int what, const char* info) { return true; }
    virtual bool onDeviceEvent(int what, const char* info) { return true; }
#endif // _WIN32_WINNT >= _WIN32_WINNT_WINXP
#if (_WIN32_WINNT >= _WIN32_WINNT_VISTA)
    virtual bool onPreshutdown() { return true; }
#endif // _WIN32_WINNT >= _WIN32_WINNT_VISTA
#endif // WIN32

    virtual ~ServiceOwner() = default;
};

} // namespace cs
