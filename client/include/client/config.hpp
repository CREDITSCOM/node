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
namespace ip = boost::asio::ip;

typedef uint16_t NodeVersion;
const NodeVersion NODE_VERSION = 86;

const std::string DEFAULT_PATH_TO_CONFIG = "config.ini";
const std::string DEFAULT_PATH_TO_DB = "test_db";
const std::string DEFAULT_PATH_TO_KEY = "keys.dat";
const std::string DEFAULT_PATH_TO_PUBLIC_KEY = "NodePublic.txt";

const uint32_t DEFAULT_MAX_NEIGHBOURS = 4;
const uint32_t DEFAULT_CONNECTION_BANDWIDTH = 1 << 19;

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

  uint32_t getMaxNeighbours() const { return maxNeighbours_; }
  uint64_t getConnectionBandwidth() const { return connectionBandwidth_; }

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
  uint32_t maxNeighbours_;
  uint64_t connectionBandwidth_;

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
