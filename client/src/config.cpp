/* Send blaming letters to @yrtimd */
#include <iostream>
#include <string>
#include <regex>
#include <stdexcept>

#include <boost/algorithm/string.hpp>
#include <boost/asio.hpp>
#include <boost/filesystem.hpp>
#include <boost/log/utility/setup/settings_parser.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/xml_parser.hpp>

#include <lib/system/logger.hpp>
#include <base58.h>
#include "config.hpp"

const std::string BLOCK_NAME_PARAMS = "params";
const std::string BLOCK_NAME_SIGNAL_SERVER = "signal_server";
const std::string BLOCK_NAME_HOST_INPUT = "host_input";
const std::string BLOCK_NAME_HOST_OUTPUT = "host_output";
const std::string BLOCK_NAME_HOST_ADDRESS = "host_address";

const std::string PARAM_NAME_NODE_TYPE = "node_type";
const std::string PARAM_NAME_BOOTSTRAP_TYPE = "bootstrap_type";
const std::string PARAM_NAME_HOSTS_FILENAME = "hosts_filename";
const std::string PARAM_NAME_USE_IPV6 = "ipv6";
const std::string PARAM_NAME_MAX_NEIGHBOURS = "max_neighbours";
const std::string PARAM_NAME_CONNECTION_BANDWIDTH = "connection_bandwidth";

const std::string PARAM_NAME_IP = "ip";
const std::string PARAM_NAME_PORT = "port";

const std::map<std::string, NodeType> NODE_TYPES_MAP = { { "client", NodeType::Client }, { "router", NodeType::Router } };
const std::map<std::string, BootstrapType> BOOTSTRAP_TYPES_MAP = { { "signal_server", BootstrapType::SignalServer }, { "list", BootstrapType::IpList } };

static EndpointData readEndpoint(const boost::property_tree::ptree& config, const std::string& propName) {
  const boost::property_tree::ptree& epTree = config.get_child(propName);

  EndpointData result;
  if (epTree.count(PARAM_NAME_IP)) {
    result.ipSpecified = true;
    result.ip = ip::make_address(epTree.get<std::string>(PARAM_NAME_IP));
  }
  else
    result.ipSpecified = false;

  result.port = epTree.get<Port>(PARAM_NAME_PORT);

  return result;
}

EndpointData EndpointData::fromString(const std::string& str) {
  static std::regex ipv4Regex("^([0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3})\\:([0-9]{1,5})$");
  static std::regex ipv6Regex("^\\[([0-9a-z\\:\\.]+)\\]\\:([0-9]{1,5})$");

  std::smatch match;
  EndpointData result { 0 };

  if (std::regex_match(str, match, ipv4Regex))
    result.ip = ip::make_address_v4(match[1]);
  else if (std::regex_match(str, match, ipv6Regex))
    result.ip = ip::make_address_v6(match[1]);
  else
    throw std::invalid_argument(str);

  result.port = std::stoul(match[2]);

  return result;
}

template <typename MapType>
typename MapType::mapped_type getFromMap(const std::string& pName, const MapType& map) {
  auto it = map.find(pName);

  if (it != map.end())
    return it->second;

  throw boost::property_tree::ptree_bad_data("Bad param value", pName);
}

Config Config::read(variables_map& vm) {
  Config result = readFromFile(vm.count("config-file") ?
                               vm["config-file"].as<std::string>() :
                               DEFAULT_PATH_TO_CONFIG);

  result.pathToDb_ = vm.count("db-path") ?
    vm["db-path"].as<std::string>() :
    DEFAULT_PATH_TO_DB;

  const auto keyFile = vm.count("key-file") ?
    vm["key-file"].as<std::string>() :
    DEFAULT_PATH_TO_PUBLIC_KEY;
  std::ifstream pub(keyFile);
  
  if (pub.is_open()) {
    std::string pub58;
    std::vector<uint8_t> myPublic;
    std::getline(pub, pub58);
    pub.close();
    DecodeBase58(pub58, myPublic);
    if (myPublic.size() != 32) {
      result.good_ = false;
      LOG_ERROR("Bad Base-58 Public Key in " << keyFile);
    }

    std::copy(myPublic.begin(), myPublic.end(), result.publicKey_.begin());
  }
  else {
    srand(time(nullptr));
    for (int i = 0; i < 32; ++i) {
      *(result.publicKey_.data() + i) = (char)(rand() % 255);
    }
  }

  return result;
}

Config Config::readFromFile(const std::string& fileName) {
  Config result;

  boost::property_tree::ptree config;

  try {
    auto ext = boost::filesystem::extension(fileName);
    boost::algorithm::to_lower(ext);
    if (ext == "json") {
      boost::property_tree::read_json(fileName, config);
    }
    else if (ext == "xml") {
      boost::property_tree::read_xml(fileName, config);
    }
    else {
      boost::property_tree::read_ini(fileName, config);
    }

    result.inputEp_ = readEndpoint(config,
                                   BLOCK_NAME_HOST_INPUT);

    if (config.count(BLOCK_NAME_HOST_OUTPUT)) {
      result.outputEp_ = readEndpoint(config,
                                      BLOCK_NAME_HOST_OUTPUT);

      result.twoSockets_ = true;/*(result.outputEp_.ip != result.inputEp_.ip ||
                                  result.outputEp_.port != result.inputEp_.port);*/
    }
    else
      result.twoSockets_ = false;

    const boost::property_tree::ptree& params =
      config.get_child(BLOCK_NAME_PARAMS);

    result.ipv6_ = !(params.count(PARAM_NAME_USE_IPV6) &&
                     params.get<std::string>(PARAM_NAME_USE_IPV6) == "false");

    result.maxNeighbours_ = params.count(PARAM_NAME_MAX_NEIGHBOURS) ?
      params.get<uint32_t>(PARAM_NAME_MAX_NEIGHBOURS) :
      DEFAULT_MAX_NEIGHBOURS;

    result.connectionBandwidth_ = params.count(PARAM_NAME_CONNECTION_BANDWIDTH) ?
      params.get<uint64_t>(PARAM_NAME_CONNECTION_BANDWIDTH) :
      DEFAULT_CONNECTION_BANDWIDTH;

    result.nType_ = getFromMap(params.get<std::string>(PARAM_NAME_NODE_TYPE),
                               NODE_TYPES_MAP);

    if (config.count(BLOCK_NAME_HOST_ADDRESS)) {
      result.hostAddressEp_ = readEndpoint(config,
                                           BLOCK_NAME_HOST_ADDRESS);
      result.symmetric_ = false;
    }
    else
      result.symmetric_ = true;

    result.bType_ = getFromMap(params.get<std::string>(PARAM_NAME_BOOTSTRAP_TYPE),
                               BOOTSTRAP_TYPES_MAP);

    if (result.bType_ == BootstrapType::SignalServer ||
        result.nType_ == NodeType::Router)
      result.signalServerEp_ = readEndpoint(config,
                                            BLOCK_NAME_SIGNAL_SERVER);
    if (result.bType_ == BootstrapType::IpList) {
      const auto hostsFileName = params.get<std::string>(PARAM_NAME_HOSTS_FILENAME);

      std::string line;

      std::ifstream hostsFile;
      hostsFile.exceptions(std::ifstream::failbit);
      hostsFile.open(hostsFileName);
      hostsFile.exceptions(std::ifstream::goodbit);

      while (getline(hostsFile, line))
        if (!line.empty())
          result.bList_.push_back(EndpointData::fromString(line));

      if (result.bList_.empty())
        throw std::length_error("No hosts specified");
    }

    result.setLoggerSettings(config);
    result.good_ = true;
  }
  catch (boost::property_tree::ini_parser_error& e) {
    LOG_ERROR("Couldn't read config file \"" << fileName << "\": " << e.what());
    result.good_ = false;
  }
  catch (boost::property_tree::ptree_bad_data& e) {
    LOG_ERROR(e.what() << ": " << e.data<std::string>());
  }
  catch (boost::property_tree::ptree_error& e) {
    LOG_ERROR("Errors in config file: " << e.what());
  }
  catch (std::invalid_argument& e) {
    LOG_ERROR("Parsing error at \"" << e.what() << "\".");
  }
  catch (std::ifstream::failure& e) {
    LOG_ERROR("Cannot open file: " << e.what());
  }
  catch (std::exception& e) {
    LOG_ERROR(e.what());
  }
  catch (...) {
    LOG_ERROR("Errors in config file");
  }

  return result;
}

void Config::setLoggerSettings(const boost::property_tree::ptree& config) {
  boost::property_tree::ptree settings;
  const auto core = config.get_child_optional("Core");
  if (core) {
    settings.add_child("Core", *core);
  }
  const auto sinks = config.get_child_optional("Sinks");
  if (sinks) {
    for (const auto& val: *sinks) {
      settings.add_child(boost::property_tree::ptree::path_type("Sinks." + val.first, '/'), val.second);
    }
  }
  for (const auto& item: config) {
    if (item.first.find("Sinks.") == 0)  
      settings.add_child(boost::property_tree::ptree::path_type(item.first, '/'), item.second);
  }
  std::stringstream ss;
  boost::property_tree::write_ini(ss, settings);
  loggerSettings_ = boost::log::parse_settings(ss);
}

const boost::log::settings& Config::getLoggerSettings() const {
  return loggerSettings_;
}
