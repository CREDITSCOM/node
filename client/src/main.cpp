/* Send blaming letters to @yrtimd */

#include <iomanip>
#include <iostream>
#ifndef WIN32
#include <signal.h>
#include <unistd.h>
#else
#include <csignal>
#endif

#include <lib/system/logger.hpp>
#include <csnode/node.hpp>
#include <net/transport.hpp>

#include "config.hpp"


volatile std::sig_atomic_t gSignalStatus = 0;
static void stopNode() noexcept(false);

#ifdef BUILD_WITH_GPROF
#include <dlfcn.h>
#include <signal.h>

void sigUsr1Handler(int sig) {
  std::cerr << "Exiting on SIGUSR1\n";
  auto _mcleanup = (void (*)(void))dlsym(RTLD_DEFAULT, "_mcleanup");
  if (_mcleanup == NULL) {
    std::cerr << "Unable to find gprof exit hook\n";
  } else {
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
  SetConsoleMode(hConsole, prevMode & (~ENABLE_QUICK_EDIT_MODE));
#endif
}

#ifndef WIN32
extern "C" static void sigHandler(int sig) {
  gSignalStatus = 1;
  std::cout << "+++++++++++++++++ >>> Signal received!!! <<< +++++++++++++++++++++++++" << std::endl;
  switch (sig)
  {
  case SIGINT:
    LOG_WARN("Signal SIGINT received, exiting");
    break;
  case SIGTERM:
    LOG_WARN("Signal SIGTERM received, exiting");
    break;
  case SIGQUIT:
    LOG_WARN("Signal SIGBREAK received, exiting");
    break;
  case SIGHUP:
    LOG_WARN("Signal SIGHUP received, exiting");
    break;
  default:
    LOG_WARN("Uncknown signal received, exiting");
    break;
  }
}

void installSignalHandler() {
  if (SIG_ERR == signal(SIGTERM, sigHandler)) {
    // Handle error
    LOG_ERROR("Error to set SIGTERM!");
    exit(EXIT_FAILURE);
  }
  if (SIG_ERR == signal(SIGQUIT, sigHandler)) {
    LOG_ERROR("Error to set SIGQUIT!");
    exit(EXIT_FAILURE);
  }
  if (SIG_ERR == signal(SIGINT, sigHandler)) {
    LOG_ERROR("Error to set SIGINT!");
    exit(EXIT_FAILURE);
  }
  if (SIG_ERR == signal(SIGHUP, sigHandler)) {
    LOG_ERROR("Error to set SIGHUP!");
    exit(EXIT_FAILURE);
  }
  if (SIG_ERR == signal(SIGBUS, sigHandler)) {
    LOG_ERROR("Error to set SIGHUP!");
    exit(EXIT_FAILURE);
  }
}
#else
BOOL WINAPI CtrlHandler(DWORD fdwCtrlType)
{
  gSignalStatus = 1;
  std::cout << "+++++++++++++++++ >>> Signal received!!! <<< +++++++++++++++++++++++++" << std::endl;
  switch (fdwCtrlType)
  {
    // Handle the CTRL-C signal. 
  case CTRL_C_EVENT:
    LOG_WARN("Ctrl-C event\n\n");
    return TRUE;

  // CTRL-CLOSE: confirm that the user wants to exit. 
  case CTRL_CLOSE_EVENT:
    LOG_WARN("Ctrl-Close event\n\n");
    return TRUE;

      // Pass other signals to the next handler. 
  case CTRL_BREAK_EVENT:
    LOG_WARN("Ctrl-Break event\n\n");
    return TRUE;

  case CTRL_LOGOFF_EVENT:
    LOG_WARN("Ctrl-Logoff event\n\n");
    return FALSE;

  case CTRL_SHUTDOWN_EVENT:
    LOG_WARN("Ctrl-Shutdown event\n\n");
    return FALSE;

  default:
    return FALSE;
  }
}
#endif // !WIN32

// Signal transport to stop and stop Node
static void stopNode() noexcept(false) {
  Transport::stop();
}

// Called periodically to poll the signal flag.
void poll_signal_flag() {
  if (gSignalStatus == 1) {
    gSignalStatus = 0;
    try {
      stopNode();
    }
    catch (...) {
      // Handle error
      LOG_ERROR("Poll signal error!");
      std::raise(SIGABRT);
    }
  }
}

int main(int argc, char* argv[]) {
#ifdef WIN32
  if (SetConsoleCtrlHandler(CtrlHandler, TRUE))
  {
    std::cout << "\n\n\n\tThe Control Handler is installed.\n" << std::flush;
    std::cout << "\n\t !!! To STOP NODE try pressing Ctrl+C or Ctrl+Break, or" << std::flush;
    std::cout << "\n\t !!! try logging off or closing the console...\n" << std::flush;
    Sleep(2000);
  }
  else
  {
    std::cout << "\nERROR: Could not set control handler" << std::flush;
    return 1;
  }
#else
  installSignalHandler();
  std::cout << "\n\n\n\tThe Control Handler is installed.\n" << std::flush;
  std::cout << "\n\t !!! To STOP NODE try pressing Ctrl+C or Ctrl+Break, or" << std::flush;
  std::cout << "\n\t !!! try logging off or closing the console...\n" << std::flush;
  sleep(2);
#endif // WIN32
  mouseSelectionDisable();
#if BUILD_WITH_GPROF
  signal(SIGUSR1, sigUsr1Handler);
#endif
  std::ios_base::sync_with_stdio(false);

  po::options_description desc("Allowed options");
  desc.add_options()
    ("help", "produce this message")
    ("db-path", po::value<std::string>(), "path to DB (default: \"test_db/\")")
    ("config-file", po::value<std::string>(), "path to configuration file (default: \"config.ini\"), supported formats: json, xml, ini");

  po::variables_map vm;

  try {
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);
  }
  catch (boost::program_options::unknown_option& e) {
    cserror() << e.what();
    cslog() << desc;
    return 1;
  }

  if (vm.count("help")) {
    cslog() << desc;
    return 0;
  }

  auto config = Config::read(vm);

  if (!config.isGood()) {
    panic();
  }

  logger::initialize(config.getLoggerSettings());

  Node node(config);

  if (!node.isGood()) {
    panic();
  }
    
  node.run();

  LOG_WARN("+++++++++++++>>> NODE ATTEMPT TO STOP! <<<++++++++++++++++++++++");
  node.stop();

  LOG_WARN("Exiting Main Function");

  logger::cleanup();

  std::cout << "Logger cleaned" << std::endl;
  std::_Exit(EXIT_SUCCESS);
}
