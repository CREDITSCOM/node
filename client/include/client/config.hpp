/* Send blaming letters to @yrtimd */
#ifndef __CONFIG_HPP__
#define __CONFIG_HPP__

#include <lib/system/keys.hpp>
#include <lib/system/common.hpp>

typedef uint16_t NodeVersion;
const NodeVersion NODE_VERSION = 86;

const std::string DEFAULT_PATH_TO_CONFIG = "config.ini";
const std::string DEFAULT_PATH_TO_DB = "test_db";
const std::string DEFAULT_PATH_TO_KEY = "keys.dat";
const std::string DEFAULT_PATH_TO_PUBLIC_KEY = "NodePublic.txt";

const uint32_t DEFAULT_MAX_NEIGHBOURS = 4;
const uint32_t DEFAULT_CONNECTION_BANDWIDTH = 1 << 19;

typedef short unsigned Port;
namespace boost
{
	namespace program_options
	{
		class variables_map;
	}
}
struct EndpointData;

enum NodeType {
  Client,
  Router
};

enum BootstrapType {
  SignalServer,
  IpList
};

class Config 
{
public:
  Config();
  Config(const Config&);
  const Config& operator=(const Config& other);
  Config(Config&&) noexcept;
  ~Config();
  void Swap(Config& other) noexcept;

  using variables_map = boost::program_options::variables_map;

  static Config read(variables_map&);

  const EndpointData& getInputEndpoint() const;
  const EndpointData& getOutputEndpoint() const;
  const EndpointData& getSignalServerEndpoint() const;
  BootstrapType getBootstrapType() const;
  NodeType getNodeType() const;
  const std::vector<EndpointData>& getIpList() const;
  const cs::PublicKey& getMyPublicKey() const;
  const std::string& getPathToDB() const;
  bool isGood() const;
  bool useIPv6() const;
  bool hasTwoSockets() const;
  uint32_t getMaxNeighbours() const;
  uint64_t getConnectionBandwidth() const;
  bool isSymmetric() const;
  const EndpointData& getAddressEndpoint() const;
  const boost::log::settings& getLoggerSettings() const;

private:
  static Config readFromFile(const std::string& fileName);
  void setLoggerSettings(const boost::property_tree::ptree& config);
private:
  struct ConfigImpl;
  std::unique_ptr<ConfigImpl> m_pImpl;
};

#endif // __CONFIG_HPP__
