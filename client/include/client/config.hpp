#ifndef __CONFIG_HPP__
#define __CONFIG_HPP__
#include <string>
#include <boost/asio.hpp>

using namespace boost::asio;

typedef uint16_t NodeVersion;
const NodeVersion NODE_VERSION = 70;

typedef short unsigned Port;

struct EndpointData {
  bool ipSpecified;

  ip::address ip;
  short unsigned port;

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
  Config(const Config&) = default;
  Config(Config&&) = default;

  static Config readFromFile(const std::string& fileName);

  const EndpointData& getInputEndpoint() const { return inputEp_; }
  const EndpointData& getOutputEndpoint() const { return outputEp_; }

  BootstrapType getBootstrapType() const { return bType_; }
  NodeType getNodeType() const { return nType_; }
  const std::vector<EndpointData>& getIpList() const { return bList_; }

  bool isGood() const { return good_; }

  bool useIPv6() const { return ipv6_; }
  bool hasTwoSockets() const { return twoSockets_; }

  bool isSymmetric() const { return symmetric_; }
  const EndpointData& getAddressEndpoint() const { return hostAddressEp_; }

private:
  Config() { }

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
};

#endif // __CONFIG_HPP__
