/* Send blaming letters to @yrtimd */
#ifndef __CONFIG_HPP__
#define __CONFIG_HPP__
#include <string>
#include <boost/asio.hpp>
#include <boost/log/utility/setup/settings.hpp>
#include <boost/program_options.hpp>

#include <lib/system/keys.hpp>
#include <lib/system/common.hpp>

namespace po = boost::program_options;
using namespace boost::asio;

typedef uint16_t NodeVersion;
const NodeVersion NODE_VERSION = 70;

const std::string DEFAULT_PATH_TO_CONFIG = "config.ini";
const std::string DEFAULT_PATH_TO_DB = "test_db";
const std::string DEFAULT_PATH_TO_KEY = "keys.dat";

typedef short unsigned Port;

struct EndpointData {
  bool ipSpecified;
  short unsigned port;
  ip::address ip;

  static EndpointData fromString(const std::string&);
};

enum NodeType {
  Client,
  Router
};

enum BootstrapType {
  SignalServer,
  IpList
};

class Config {
public:
  Config() {} // necessary for testing
  Config(const Config&) = default;
  Config(Config&&) = default;

  static Config read(po::variables_map&);

  const EndpointData& getInputEndpoint() const { return inputEp_; }
  const EndpointData& getOutputEndpoint() const { return outputEp_; }

  const EndpointData& getSignalServerEndpoint() const { return signalServerEp_; }

  BootstrapType getBootstrapType() const { return bType_; }
  NodeType getNodeType() const { return nType_; }
  const std::vector<EndpointData>& getIpList() const { return bList_; }

  const cs::PublicKey& getMyPublicKey() const { return publicKey_; }
  const std::string& getPathToDB() const { return pathToDb_; }

  bool isGood() const { return good_; }

  bool useIPv6() const { return ipv6_; }
  bool hasTwoSockets() const { return twoSockets_; }

  bool isSymmetric() const { return symmetric_; }
  const EndpointData& getAddressEndpoint() const { return hostAddressEp_; }

  const boost::log::settings& getLoggerSettings() const;

private:
  static Config readFromFile(const std::string& fileName);
  void setLoggerSettings(const boost::property_tree::ptree& config);

  bool good_ = false;

  EndpointData inputEp_;

  bool twoSockets_;
  EndpointData outputEp_;

  NodeType nType_;

  bool ipv6_;

  bool symmetric_;
  EndpointData hostAddressEp_;

  BootstrapType bType_;
  EndpointData signalServerEp_;

  std::vector<EndpointData> bList_;
  bool server_;

  std::string pathToDb_;
  cs::PublicKey publicKey_;

  boost::log::settings loggerSettings_;
};

#endif // __CONFIG_HPP__
