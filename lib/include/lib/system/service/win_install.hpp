#pragma once

#include <string>
#include <system_error>

#include <windows.h>

namespace cs {
namespace helpers {
class ScmHandler {
public:
    ScmHandler(SC_HANDLE h) : h_(h) {}
    ~ScmHandler() { close();  }

    bool isOpen() {
        return h_ != nullptr;
    }

    bool close() {
        if (!isOpen()) return false;
        bool res = CloseServiceHandle(h_);
        h_ = nullptr;
        return res;
    }

    operator SC_HANDLE() {
        return h_;
    }

private:
    SC_HANDLE h_;
};
} // namespace helpers

inline std::error_code installService(
    const std::string& name,
    const std::string& binaryPath,
    const std::string& params = std::string(),
    DWORD startupType = SERVICE_AUTO_START,
    DWORD errorControl = SERVICE_ERROR_NORMAL,
    LPSTR dependencies = nullptr,
    BOOL enableAutoRestartAfterFail = FALSE
) {
    if (name.empty() || binaryPath.empty()) {
        return std::make_error_code(std::errc::invalid_argument);
    }

    helpers::ScmHandler scmHandler(OpenSCManager(NULL, NULL, SC_MANAGER_CREATE_SERVICE));
    if (!scmHandler.isOpen()) {
        return std::error_code(static_cast<int>(GetLastError()), std::system_category());
    }

    std::string startCommand;
    startCommand = "\"" + binaryPath + "\"";
    if (!params.empty()) {
        startCommand += " " + params;
    }

    helpers::ScmHandler serviceHandler(
        CreateServiceA(
            scmHandler,
            name.c_str(),
            name.c_str(),
            0,
            SERVICE_WIN32_OWN_PROCESS,
            startupType,
            errorControl,
            startCommand.c_str(),
            nullptr,
            nullptr,
            dependencies,
            nullptr,
            nullptr
        )
    );

    if (!serviceHandler.isOpen()) {
        return std::error_code(static_cast<int>(GetLastError()), std::system_category());
    }

    if (enableAutoRestartAfterFail) {
        helpers::ScmHandler serviceHandler2(OpenServiceA(scmHandler, name.c_str(), SERVICE_ALL_ACCESS));
        if (!serviceHandler2.isOpen()) {
            return std::error_code(static_cast<int>(GetLastError()), std::system_category());
        }

        SC_ACTION failActions[3];
        failActions[0].Type = SC_ACTION_RESTART;
        failActions[0].Delay = 1000;
        failActions[1].Type = SC_ACTION_RESTART;
        failActions[1].Delay = 5000;
        failActions[2].Type = SC_ACTION_RESTART;
        failActions[2].Delay = 15000;

        SERVICE_FAILURE_ACTIONS serviceFailActions;
        serviceFailActions.dwResetPeriod = 24 * 60 * 60;
        serviceFailActions.lpCommand = nullptr;
        serviceFailActions.lpRebootMsg = nullptr;
        serviceFailActions.cActions = 3;
        serviceFailActions.lpsaActions = failActions;

        if (!ChangeServiceConfig2(serviceHandler2, SERVICE_CONFIG_FAILURE_ACTIONS, &serviceFailActions)) {
            auto error = static_cast<int>(GetLastError());
            DeleteService(serviceHandler);
            return std::error_code(error, std::system_category());
        }
    }

    return std::error_code();
}

inline std::error_code uninstallService(const std::string& name) {
    if (name.empty()) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    
    helpers::ScmHandler scmHandler(OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT));
    if (!scmHandler.isOpen()) {
        return std::error_code(static_cast<int>(GetLastError()), std::system_category());
    }

    helpers::ScmHandler serviceHandler(
        OpenServiceA(
            scmHandler,
            name.c_str(),
            SERVICE_STOP|DELETE
        )
    );
    if (!serviceHandler.isOpen()) {
        return std::error_code(static_cast<int>(GetLastError()), std::system_category());
    }

    SERVICE_STATUS serviceStatus;
    ControlService(serviceHandler, SERVICE_CONTROL_STOP, &serviceStatus);
    Sleep(500);
    if (!DeleteService(serviceHandler)) {
        return std::error_code(static_cast<int>(GetLastError()), std::system_category());
    }

    return std::error_code();
}

} // namespace cs