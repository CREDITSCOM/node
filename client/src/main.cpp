/* Send blaming letters to @yrtimd */
#include "stdafx.h"

#include <lib/system/logger.hpp>
#include <csnode/node.hpp>

#include "config.hpp"

#ifdef BUILD_WITH_GPROF
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

int main(int argc, char* argv[]) {
  mouseSelectionDisable();
#if BUILD_WITH_GPROF
  signal(SIGUSR1, sigUsr1Handler);
#endif
  std::ios_base::sync_with_stdio(false);

  using namespace boost::program_options;
  options_description desc("Allowed options");
  desc.add_options()
    ("help", "produce this message")
    ("db-path", boost::program_options::value<std::string>(), "path to DB (default: \"test_db/\")")
    ("config-file", boost::program_options::value<std::string>(), "path to configuration file (default: \"config.ini\"), supported formats: json, xml, ini");

  variables_map vm;

  try {
    store(parse_command_line(argc, argv, desc), vm);
    notify(vm);
  }
  catch (unknown_option& e) {
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

  logger::cleanup();
  return 0;
}
