/* Send blaming letters to @yrtimd */
#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <boost/asio.hpp>
#include <boost/log/utility/setup/settings.hpp>
#include <boost/program_options.hpp>
#include <string>

#include <lib/system/common.hpp>
#include <net/neighbourhood.hpp> // using Neighbourhood::MaxNeighbours constant

namespace po = boost::program_options;
namespace ip = boost::asio::ip;

typedef uint16_t NodeVersion;
const NodeVersion NODE_VERSION = 415;

const std::string DEFAULT_PATH_TO_CONFIG = "config.ini";
const std::string DEFAULT_PATH_TO_DB = "test_db";
const std::string DEFAULT_PATH_TO_KEY = "keys.dat";

const std::string DEFAULT_PATH_TO_PUBLIC_KEY = "NodePublic.txt";
const std::string DEFAULT_PATH_TO_PRIVATE_KEY = "NodePrivate.txt";

const uint32_t DEFAULT_MAX_NEIGHBOURS = Neighbourhood::MaxNeighbours;
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

struct PoolSyncData {
    bool oneReplyBlock = true;                      // true: sendBlockRequest one pool at a time. false: equal to number of pools requested.
    bool isFastMode = false;                        // true: is silent mode synchro(sync up to the current round). false: normal mode
    uint8_t blockPoolsCount = 25;                   // max block count in one request: cannot be 0
    uint8_t requestRepeatRoundCount = 20;           // round count for repeat request : 0-never
    uint8_t neighbourPacketsCount = 10;             // packet count for connect another neighbor : 0-never
    uint16_t sequencesVerificationFrequency = 350;  // sequences received verification frequency : 0-never; 1-once per round: other- in ms;
};

struct ApiData {
    uint16_t port = 9090;
    uint16_t ajaxPort = 8081;
    uint16_t executorPort = 9080;
    uint16_t apiexecPort = 9070;
};

class Config {
public:
    Config() {
    }  // necessary for testing
    Config(const Config&) = default;
    Config(Config&&) = default;

    static Config read(po::variables_map&);

    const EndpointData& getInputEndpoint() const {
        return inputEp_;
    }
    const EndpointData& getOutputEndpoint() const {
        return outputEp_;
    }

    const EndpointData& getSignalServerEndpoint() const {
        return signalServerEp_;
    }

    BootstrapType getBootstrapType() const {
        return bType_;
    }
    NodeType getNodeType() const {
        return nType_;
    }
    const std::vector<EndpointData>& getIpList() const {
        return bList_;
    }

    const std::string& getPathToDB() const {
        return pathToDb_;
    }

    bool isGood() const {
        return good_;
    }

    bool useIPv6() const {
        return ipv6_;
    }
    bool hasTwoSockets() const {
        return twoSockets_;
    }

    uint32_t getMaxNeighbours() const {
        return maxNeighbours_;
    }
    uint64_t getConnectionBandwidth() const {
        return connectionBandwidth_;
    }

    bool isSymmetric() const {
        return symmetric_;
    }
    const EndpointData& getAddressEndpoint() const {
        return hostAddressEp_;
    }

    const boost::log::settings& getLoggerSettings() const {
        return loggerSettings_;
    }

    const PoolSyncData& getPoolSyncSettings() const {
        return poolSyncData_;
    }

    const ApiData& getApiSettings() const {
        return apiData_;
    }

    const cs::PublicKey& getMyPublicKey() const {
        return publicKey_;
    }
    const cs::PrivateKey& getMyPrivateKey() const {
        return privateKey_;
    }

    static NodeVersion getNodeVersion() {
        return NODE_VERSION;
    }

    void dumpJSONKeys(const std::string& fName) const;

private:
    static Config readFromFile(const std::string& fileName);
    void setLoggerSettings(const boost::property_tree::ptree& config);
    void readPoolSynchronizerData(const boost::property_tree::ptree& config);
    void readApiData(const boost::property_tree::ptree& config);

    bool readKeys(const std::string& pathToPk, const std::string& pathToSk, const bool encrypt);
    void showKeys(const std::string& pk58);
    
    void changePasswordOption(const std::string& pathToSk);

    template <typename T>
    bool checkAndSaveValue(const boost::property_tree::ptree& data, const std::string& block, const std::string& param, T& value);

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

    std::string pathToDb_;

    cs::PublicKey publicKey_;
    cs::PrivateKey privateKey_;

    boost::log::settings loggerSettings_;

    PoolSyncData poolSyncData_;
    ApiData apiData_;
};

#endif  // CONFIG_HPP
