/* Send blaming letters to @yrtimd */
#include <regex>
#include <stdexcept>

#include <boost/asio.hpp>
#include <boost/property_tree/ini_parser.hpp>

#include <base58.h>
#include <sodium.h>

#include <lib/system/logger.hpp>
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

const std::string ARG_NAME_CONFIG_FILE = "config-file";
const std::string ARG_NAME_DB_PATH = "db-path";
const std::string ARG_NAME_PUBLIC_KEY_FILE = "public-key-file";
const std::string ARG_NAME_PRIVATE_KEY_FILE = "private-key-file";

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
  EndpointData result;

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

static inline std::string getArgFromCmdLine(const po::variables_map& vm,
                                            const std::string& name,
                                            const std::string& defVal) {
  return vm.count(name) ? vm[name].as<std::string>() : defVal;
}

static inline void writeFile(const std::string name, const std::string data) {
  std::ofstream file(name);
  file << data;
  file.close();
}

Config Config::read(po::variables_map& vm) {
  Config result = readFromFile(getArgFromCmdLine(vm,
                                                 ARG_NAME_CONFIG_FILE,
                                                 DEFAULT_PATH_TO_CONFIG));

  result.pathToDb_ = getArgFromCmdLine(vm,
                                       ARG_NAME_DB_PATH,
                                       DEFAULT_PATH_TO_DB);

  if (result.good_)
    result.good_ =
      result.readKeys(getArgFromCmdLine(vm, ARG_NAME_PUBLIC_KEY_FILE, DEFAULT_PATH_TO_PUBLIC_KEY),
                      getArgFromCmdLine(vm, ARG_NAME_PRIVATE_KEY_FILE, DEFAULT_PATH_TO_PRIVATE_KEY));

  return result;
}

bool Config::readKeys(const std::string& pathToPk, const std::string& pathToSk) {
  // First read private
  std::ifstream skFile(pathToSk);
  std::string pk58;

  if (skFile.is_open()) {
    std::string sk58;
    std::vector<uint8_t> sk;

    std::getline(skFile, sk58);
    skFile.close();
    DecodeBase58(sk58, sk);

    if (sk.size() != PRIVATE_KEY_LENGTH) {
      LOG_ERROR("Bad Base-58 Private Key in " << pathToSk);
      return false;
    }

    privateKey_ = PrivateKey((const char*)sk.data());
    crypto_sign_ed25519_sk_to_pk((unsigned char*)publicKey_.str, (unsigned char*)privateKey_.str);
    pk58 = EncodeBase58((unsigned char*)publicKey_.str,
                        (unsigned char*)(publicKey_.str + PUBLIC_KEY_LENGTH));
  }
  else {
    // No private key detected
    for (;;) {
      std::cout << "No suitable keys were found. Type \"g\" to generate or \"q\" to quit." << std::endl;
      char flag;
      std::cin >> flag;

      if (flag == 'g') {
        // Key generation
        crypto_sign_ed25519_keypair((unsigned char*)publicKey_.str,
                                    (unsigned char*)privateKey_.str);
        pk58 = EncodeBase58((unsigned char*)publicKey_.str,
                            (unsigned char*)(publicKey_.str + PUBLIC_KEY_LENGTH));

        writeFile(pathToPk, pk58);
        writeFile(pathToSk, EncodeBase58((unsigned char*)privateKey_.str,
                                         (unsigned char*)(privateKey_.str + PRIVATE_KEY_LENGTH)));

        LOG_EVENT("Keys generated");
        break;
      }
      else if (flag == 'q')
        return false;
    }
  }

  // Okay so by now we have both public and private key fields filled up
  std::ifstream pkFile(pathToPk);
  bool pkGood = false;

  if (pkFile.is_open()) {
    std::string pkFileCont;
    std::getline(pkFile, pkFileCont);
    pkFile.close();

    pkGood = (pkFileCont == pk58);
  }

  if (!pkGood) {
    std::cout << "The PUBLIC key file not found or doesn't contain a valid key (matching the provided private key). Type \"f\" to rewrite the PUBLIC key file." << std::endl;
    char flag;
    std::cin >> flag;
    if (flag == 'f')
      writeFile(pathToPk, pk58);
    else
      return false;
  }

  return true;
}

Config Config::readFromFile(const std::string& fileName) {
  Config result;

  boost::property_tree::ptree config;

  try {
    boost::property_tree::read_ini(fileName, config);

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
