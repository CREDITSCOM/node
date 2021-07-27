#include <peer.hpp>

#include <net/transport.hpp>
#include <csnode/configholder.hpp>
#include <cmdlineargs.hpp>

#ifdef WIN32
#include <csignal>
#endif

namespace {
#ifdef BUILD_WITH_GPROF
void sigUsr1Handler(int sig) {
    std::cerr << "Exiting on SIGUSR1\n";
    auto _mcleanup = (void (*)(void))dlsym(RTLD_DEFAULT, "_mcleanup");
    if (_mcleanup == NULL) {
        std::cerr << "Unable to find gprof exit hook\n";
    }
    else {
        _mcleanup();
    }
    _exit(0);
}
#endif

#ifdef _WIN32
inline void mouseSelectionDisable() {
    DWORD prevMode = 0;
    HANDLE hConsole = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(hConsole, &prevMode);
    SetConsoleMode(hConsole, prevMode & static_cast<unsigned long>(~ENABLE_QUICK_EDIT_MODE));
}

BOOL WINAPI CtrlHandler(DWORD fdwCtrlType) {
    gSignalStatus = 1;
    Node::requestStop();
    std::cout << "+++++++++++++++++ >>> Signal received!!! <<< +++++++++++++++++++++++++" << std::endl;
    switch (fdwCtrlType) {
            // Handle the CTRL-C signal.
        case CTRL_C_EVENT:
            cswarning() << "Ctrl-C event\n\n";
            return TRUE;

        // CTRL-CLOSE: confirm that the user wants to exit.
        case CTRL_CLOSE_EVENT:
            cswarning() << "Ctrl-Close event\n\n";
            return TRUE;

            // Pass other signals to the next handler.
        case CTRL_BREAK_EVENT:
            cswarning() << "Ctrl-Break event\n\n";
            return TRUE;

        case CTRL_LOGOFF_EVENT:
            cswarning() << "Ctrl-Logoff event\n\n";
            return FALSE;

        case CTRL_SHUTDOWN_EVENT:
            cswarning() << "Ctrl-Shutdown event\n\n";
            return FALSE;

        default:
            return FALSE;
    }
}
#endif // _WIN32
} // anonimous namespace

namespace cs {

Peer::Peer(
    const char* serviceName,
    Config& config,
    boost::program_options::variables_map& vm
)
    : service_(static_cast<ServiceOwner&>(*this), serviceName)
    , config_(config)
    , vm_(vm) {}

int Peer::executeProtocol() {
    bool ok = service_.run();
    if (!ok) {
#ifdef _WIN32
        auto ecode = std::error_code(static_cast<int>(GetLastError()), std::system_category());
        cslog() << "Cannot run service, last error: " << ecode.message();
#endif // _WIN32
    }
    return ok ? 0 : -1;
}

bool Peer::onInit(const char*) {
#if defined(WIN32) && defined(DISABLE_DAEMON)
    if (!SetConsoleCtrlHandler(CtrlHandler, TRUE)) {
        cslog() << "\nERROR: Could not set control handler";
        return false;
    }
#ifndef _DEBUG
    mouseSelectionDisable();
#endif // !_DEBUG
#endif  // WIN32 && DISABLE_DAEMON

#if BUILD_WITH_GPROF
    signal(SIGUSR1, sigUsr1Handler);
#endif

    logger::initialize(config_.getLoggerSettings());
    observer_ = std::make_unique<config::Observer>(config_, vm_);
    cs::ConfigHolder::instance().setConfig(config_);
    cs::Connector::connect(
        &observer_->configChanged,
        &cs::ConfigHolder::instance(),
        &cs::ConfigHolder::onConfigChanged
    );


    node_ = std::make_unique<Node>(*observer_);
    if (!node_->isGood()) {
        cserror() << "Node is not good after init";
        return false;        
    }

    cslog() << "Node initialized successfully";

    return true;
}

bool Peer::onRun(const char*) {
    if (vm_.count(cmdline::argSetBCTop) == 0) {
        cslog() << "Running Node";
        node_->run();
    }
    else {
        cslog() << "Stop after initialization";
        node_->stop();
    }

    cslog() << "Node stopped";
    cslog() << "Destroying Node";
    node_->destroy();
    node_.reset(nullptr);
    logger::cleanup();
    return true;
}

bool Peer::onStop() {
    cslog() << "STOP REQUESTED!";
    gSignalStatus = 1;
    Node::requestStop();
    return true;
}

bool Peer::onPause() {
    cslog() << "PAUSE REQUESTED! CALL STOP REQUEST!";
    return onStop();
}

} // namespace cs
