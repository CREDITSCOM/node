/* Send blaming letters to @yrtimd */
#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <string>

#include <boost/asio.hpp>
#include <boost/log/utility/setup/settings.hpp>
#include <boost/program_options.hpp>

#include <lib/system/common.hpp>

#include <net/neighbourhood.hpp> // using Neighbourhood::MaxNeighbours constant

namespace po = boost::program_options;
namespace ip = boost::asio::ip;

using NodeVersion = uint16_t;
const NodeVersion NODE_VERSION = 421;

const std::string DEFAULT_PATH_TO_CONFIG = "config.ini";
const std::string DEFAULT_PATH_TO_DB = "test_db";
const std::string DEFAULT_PATH_TO_KEY = "keys.dat";

const std::string DEFAULT_PATH_TO_PUBLIC_KEY = "NodePublic.txt";
const std::string DEFAULT_PATH_TO_PRIVATE_KEY = "NodePrivate.txt";

const uint32_t DEFAULT_MAX_NEIGHBOURS = Neighbourhood::MaxNeighbours;
const uint32_t DEFAULT_CONNECTION_BANDWIDTH = 1 << 19;
const uint32_t DEFAULT_OBSERVER_WAIT_TIME = 5 * 60 * 1000;  // ms

const size_t DEFAULT_CONVEYER_SEND_CACHE_VALUE = 10;             // rounds
const size_t DEFAULT_CONVEYER_MAX_RESENDS_SEND_CACHE = 10;       // retries

const uint8_t DELTA_ROUNDS_VERIFY_NEW_SERVER = 100;
using Port = short unsigned;

struct EndpointData {
    bool ipSpecified = false;
    short unsigned port = 0;
    ip::address ip{};

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
    int executorSendTimeout = 4000;
    int executorReceiveTimeout = 4000;
    int serverSendTimeout = 30000;
    int serverReceiveTimeout = 30000;
    int ajaxServerSendTimeout = 30000;
    int ajaxServerReceiveTimeout = 30000;
    std::string executorHost{ "localhost" };
    std::string executorCmdLine{};
};

struct ConveyerData {
    size_t sendCacheValue = DEFAULT_CONVEYER_SEND_CACHE_VALUE;
    size_t maxResendsSendCache = DEFAULT_CONVEYER_MAX_RESENDS_SEND_CACHE;
};

class Config {
public:
    Config() {
    }  // necessary for testing

    Config(const Config&) = default;
    Config(Config&&) = default;
    Config& operator=(const Config&) = default;
    Config& operator=(Config&&) = default;

    static Config read(po::variables_map&);
    
    template<typename ... Ts>
    using IsConvertToString = std::enable_if_t<(std::is_convertible_v<Ts, std::string>&& ...)>;

    template<typename T, typename ... Ts, typename = IsConvertToString<T, Ts...>>
    static bool replaceBlock(T&& blockName, Ts&& ... newLines);

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

    bool recreateIndex() const {
        return recreateIndex_;
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

    bool alwaysExecuteContracts() const {
        return alwaysExecuteContracts_;
    }

    uint64_t observerWaitTime() const {
        return observerWaitTime_;
    }

    bool readKeys(const po::variables_map& vm);
    bool enterWithSeed();

    const ConveyerData& conveyerData() const {
        return conveyerData_;
    }

    void swap(Config& config);

private:
    static Config readFromFile(const std::string& fileName);

    void setLoggerSettings(const boost::property_tree::ptree& config);
    void readPoolSynchronizerData(const boost::property_tree::ptree& config);
    void readApiData(const boost::property_tree::ptree& config);
    void readConveyerData(const boost::property_tree::ptree& config);

    bool readKeys(const std::string& pathToPk, const std::string& pathToSk, const bool encrypt);
    void showKeys(const std::string& pk58);

    void changePasswordOption(const std::string& pathToSk);

    template <typename T>
    bool checkAndSaveValue(const boost::property_tree::ptree& data, const std::string& block, const std::string& param, T& value);

    bool good_ = false;

    EndpointData inputEp_;

    bool twoSockets_ = false;

    EndpointData outputEp_;

    NodeType nType_ = NodeType::Client;

    bool ipv6_ = false;

    uint32_t maxNeighbours_ = DEFAULT_MAX_NEIGHBOURS;
    uint64_t connectionBandwidth_ = DEFAULT_CONNECTION_BANDWIDTH;

    bool symmetric_ = false;
    EndpointData hostAddressEp_;

    BootstrapType bType_ = SignalServer;
    EndpointData signalServerEp_;

    std::vector<EndpointData> bList_;

    std::string pathToDb_;

    cs::PublicKey publicKey_{};
    cs::PrivateKey privateKey_{};

    boost::log::settings loggerSettings_{};

    PoolSyncData poolSyncData_;
    ApiData apiData_;

    bool alwaysExecuteContracts_ = false;
    bool recreateIndex_ = false;

    uint64_t observerWaitTime_ = DEFAULT_OBSERVER_WAIT_TIME;

    ConveyerData conveyerData_;

    friend bool operator==(const Config&, const Config&);
};

// all operators
bool operator==(const EndpointData& lhs, const EndpointData& rhs);
bool operator!=(const EndpointData& lhs, const EndpointData& rhs);

bool operator==(const PoolSyncData& lhs, const PoolSyncData& rhs);
bool operator!=(const PoolSyncData& lhs, const PoolSyncData& rhs);

bool operator==(const ApiData& lhs, const ApiData& rhs);
bool operator!=(const ApiData& lhs, const ApiData& rhs);

bool operator==(const ConveyerData& lhs, const ConveyerData& rhs);
bool operator!=(const ConveyerData& lhs, const ConveyerData& rhs);

bool operator==(const Config& lhs, const Config& rhs);
bool operator!=(const Config& lhs, const Config& rhs);


template<typename T, typename ... Ts, typename>
bool Config::replaceBlock(T&& blockName, Ts&& ... newLines) {
    std::ifstream in(DEFAULT_PATH_TO_CONFIG, std::ios::in);

    if (!in) {
        cswarning() << "Couldn't read config file " << DEFAULT_PATH_TO_CONFIG;
        return false;
    }

    std::string newConfig = cs::Utils::readAllFileData(in);

    const std::string fullBlockName = "[" + std::string(blockName) + "]";
    const std::string fullReplaceString = fullBlockName + "\n" + ((std::string(newLines) + "\n") + ...) + "\n";

    if (const auto startPos = newConfig.find(fullBlockName); startPos != std::string::npos) {
        const auto tmpPos = newConfig.find("[", startPos + 1);
        const auto endPos = tmpPos != std::string::npos ? tmpPos - 1 : newConfig.size();

        newConfig.erase(startPos, endPos - startPos + 1);
        newConfig.insert(startPos, fullReplaceString);
    }
    else {
        newConfig += fullReplaceString;
    }

    in.close();

    std::ofstream out(DEFAULT_PATH_TO_CONFIG, std::ios::out | std::ios::trunc);

    if (!out) {
        cswarning() << "Couldn't read config file " << DEFAULT_PATH_TO_CONFIG;
        return false;
    }

    out << newConfig.data();
    out.close();

    return true;
}

#endif  // CONFIG_HPP
