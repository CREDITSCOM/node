/* Send blaming letters to @yrtimd */
#include <iomanip>
#include <iostream>

#include <boost/program_options.hpp>

#include <lib/system/logger.hpp>
#include <net/network.hpp>

#include "config.hpp"

namespace po = boost::program_options;

const std::string DEFAULT_PATH_TO_CONFIG = "config.ini";
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
    ("config-file", po::value<std::string>(), "path to configuration file");

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

  auto config = Config::readFromFile(vm.count("config-file") ?
                                     vm["config-file"].as<std::string>() :
                                     DEFAULT_PATH_TO_CONFIG);
  if (!config.isGood()) panic();

  auto& net = Network::init(config);
  if (!net.isGood()) panic();

  return 0;
}
