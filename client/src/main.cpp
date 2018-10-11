/* Send blaming letters to @yrtimd */
#include <iomanip>
#include <iostream>

#include <lib/system/logger.hpp>
#include <csnode/node.hpp>

#include "config.hpp"

const uint32_t CLOSE_TIMEOUT_SECONDS = 10;

void panic() {
  LOG_ERROR("Couldn't continue due to critical errors. The node will be closed in " << CLOSE_TIMEOUT_SECONDS << " seconds...");
  std::this_thread::sleep_for(std::chrono::seconds(CLOSE_TIMEOUT_SECONDS));
  exit(1);
}

int main(int argc, char* argv[]) {
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
    LOG_ERROR(e.what());
    std::cout << desc << std::endl;
    return 1;
  }

  if (vm.count("help")) {
    std::cout << desc << std::endl;
    return 0;
  }

  auto config = Config::read(vm);
  if (!config.isGood()) panic();

  logger::initialize(config.getLoggerSettings());

  Node node(config);
  if (!node.isGood()) panic();

  node.run(config);

  logger::cleanup();
  return 0;
}
