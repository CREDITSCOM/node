#include "stdafx.h"

#include <iomanip>
#include <iostream>
#ifndef WIN32
#include <signal.h>
#include <unistd.h>
#else
#include <csignal>
#endif

#include <csnode/node.hpp>
#include <csnode/configholder.hpp>

#include <lib/system/logger.hpp>

#include <net/transport.hpp>

#include <params.hpp>
#include <observer.hpp>
#include <version.hpp>

#include <sys/types.h>
#include <sys/stat.h>

// diagnostic output
#if defined(_MSC_VER)
#if defined(MONITOR_NODE)
#pragma message("\n*** Monitor node has been built ***\n")
#elif defined(WEB_WALLET_NODE)
#pragma message("\n*** Web wallet node has been built ***\n")
#elif defined(SPAMMER)
#pragma message("\n*** Spammer node has been built ***\n")
#else
#pragma message("\n*** Basic node has been built ***\n")
#endif
#endif  // _MSC_VER

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

const uint32_t CLOSE_TIMEOUT_SECONDS = 10;

void panic() {
    cserror() << "Couldn't continue due to critical errors. The node will be closed in " << CLOSE_TIMEOUT_SECONDS << " seconds...";
    std::this_thread::sleep_for(std::chrono::seconds(CLOSE_TIMEOUT_SECONDS));
    exit(1);
}

inline void mouseSelectionDisable() {
#if defined(WIN32) && !defined(_DEBUG)
    DWORD prevMode = 0;
    HANDLE hConsole = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(hConsole, &prevMode);
    SetConsoleMode(hConsole, prevMode & static_cast<unsigned long>(~ENABLE_QUICK_EDIT_MODE));
#endif
}

#ifndef WIN32
extern "C" void sigHandler(int sig) {
    gSignalStatus = 1;
    Node::requestStop();
    std::cout << "+++++++++++++++++ >>> Signal received!!! <<< +++++++++++++++++++++++++" << std::endl;
    switch (sig) {
        case SIGINT:
            cswarning() << "Signal SIGINT received, exiting";
            break;
        case SIGTERM:
            cswarning() << "Signal SIGTERM received, exiting";
            break;
        case SIGHUP:
            cswarning() << "Signal SIGHUP received, exiting";
            break;
        default:
            cswarning() << "Unknown signal received!!!";
            break;
    }
}

void installSignalHandler() {
    if (SIG_ERR == signal(SIGTERM, sigHandler)) {
        // Handle error
        cserror() << "Error to set SIGTERM!";
        _exit(EXIT_FAILURE);
    }
    if (SIG_ERR == signal(SIGINT, sigHandler)) {
        cserror() << "Error to set SIGINT!";
        _exit(EXIT_FAILURE);
    }
    if (SIG_ERR == signal(SIGHUP, sigHandler)) {
        cserror() << "Error to set SIGHUP!";
        _exit(EXIT_FAILURE);
    }
}
#else
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
#endif  // !WIN32

int main(int argc, char* argv[]) {
#ifdef WIN32
    if (!SetConsoleCtrlHandler(CtrlHandler, TRUE)) {
        std::cout << "\nERROR: Could not set control handler" << std::flush;
        return EXIT_FAILURE;
    }
#else
    installSignalHandler();
#endif  // WIN32
    mouseSelectionDisable();
#if BUILD_WITH_GPROF
    signal(SIGUSR1, sigUsr1Handler);
#endif
    std::ios_base::sync_with_stdio(false);

    const char* argHelp = "help";
    const char* argVersion = "version";
    const char* argDBPath = "db-path";
    const char* argSeed = "seed";
    const char* argDumpKeys = "dumpkeys";
    const char* argSetBCTop = "set-bc-top";
    const char* kDeprecatedDBPath = "test_db";

    using namespace boost::program_options;
    options_description desc("Allowed options");
    desc.add_options()
        (argHelp, "produce this message")
        ("recreate-index", "recreate index.db")
        (argSeed, "enter with seed instead of keys")
        (argSetBCTop, po::value<uint64_t>(), "all blocks in blockchain with higher sequence will be removed")
        ("disable-auto-shutdown", "node will be prohibited to shutdown in case of fatal errors")
        ("version", "show node version")
        ("db-path", po::value<std::string>(), "path to DB (default: \"db/\")")
        ("config-file", po::value<std::string>(), "path to configuration file (default: \"config.ini\")")
        ("public-key-file", po::value<std::string>(), "path to public key file (default: \"NodePublic.txt\")")
        ("private-key-file", po::value<std::string>(), "path to private key file (default: \"NodePrivate.txt\")")
        ("dumpkeys", po::value<std::string>(), "dump your public and private keys into a JSON file with the specified name (UNENCRYPTED!)")
        ("encryptkey", "encrypts the private key with password upon startup (if not yet encrypted)");

    variables_map vm;
    try {
        store(parse_command_line(argc, argv, desc), vm);
        notify(vm);
    }
    catch (unknown_option& e) {
        cserror() << e.what();
        cslog() << desc;
        return EXIT_FAILURE;
    }
    catch (invalid_command_line_syntax& e) {
        cserror() << e.what();
        cslog() << desc;
        return EXIT_FAILURE;
    }
    catch (...) {
        cserror() << "Couldn't parse the arguments";
        cslog() << desc;
        return EXIT_FAILURE;
    }

    if (vm.count(argHelp) > 0) {
        cslog() << desc;
        return EXIT_SUCCESS;
    }

    // in case of version option print info and exit
    if (vm.count(argVersion) > 0) {
        cslog() << "Node version is " << Config::getNodeVersion();
#ifdef MONITOR_NODE
        cslog() << "Monitor version";
#endif
#ifdef WEB_WALLET_NODE
        cslog() << "Wallet version";
#endif
        cslog() << "Git info:";
        cslog() << "Build SHA1: " << client::Version::GIT_SHA1;
        cslog() << "Date: " << client::Version::GIT_DATE;
        cslog() << "Subject: " << client::Version::GIT_COMMIT_SUBJECT;
        return EXIT_SUCCESS;
    }

    // test db directory, exit if user did not rename old kDeprecatedDBPath and expect to use it as default one
    if (vm.count(argDBPath) == 0) {
        // arg is not set, so default dir is not "db_test"
        struct stat info;
        if (stat(kDeprecatedDBPath, &info) == 0) {
            if (info.st_mode & S_IFDIR) {
                cslog() << "Deprecated blockchain path \'" << kDeprecatedDBPath
                    << "\' is in current directory. Please rename it to \'db\' to use it as default storage, or rename to any other not to use at all, then restart your node again";
                return EXIT_FAILURE;
            }
        } 
    }

    if (!cscrypto::cryptoInit()) {
        std::cout << "Couldn't initialize the crypto library" << std::endl;
        panic();
    }

    auto config = Config::read(vm);

    if (!config.isGood()) {
        panic();
    }

    if (vm.count(argSeed) == 0) {
        if (!config.readKeys(vm)) {
            return EXIT_FAILURE;
        }
    }
    else {
        if (!config.enterWithSeed()) {
            return EXIT_FAILURE;
        }
    }

    if (vm.count(argDumpKeys) > 0) {
        auto fName = vm[argDumpKeys].as<std::string>();
        if (fName.size() > 0) {
            config.dumpJSONKeys(fName);
            cslog() << "Keys dumped to " << fName;
            return EXIT_SUCCESS;
        }
    }

    logger::initialize(config.getLoggerSettings());

    cs::config::Observer observer(config, vm);
    cs::ConfigHolder::instance().setConfig(config);
    cs::Connector::connect(&observer.configChanged, &cs::ConfigHolder::instance(), &cs::ConfigHolder::onConfigChanged);

    Node node(observer);

    if (!node.isGood()) {
        panic();
    }

    if (vm.count(argSetBCTop) > 0) {
        node.stop();
        logger::cleanup();
        std::_Exit(EXIT_SUCCESS);
    }

    std::cout << "Running Node\n";
    node.run();

    cswarning() << "+++++++++++++>>> NODE ATTEMPT TO STOP! <<<++++++++++++++++++++++";
    node.destroy();

    cswarning() << "Exiting Main Function";

    logger::cleanup();

    std::cout << "Logger cleaned" << std::endl;
    std::_Exit(EXIT_SUCCESS);
}
