/* Send blaming letters to @yrtimd */
#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <string>

#include <boost/asio.hpp>
#include <boost/log/utility/setup/settings.hpp>
#include <boost/program_options.hpp>

#include <lib/system/common.hpp>
#include <lib/system/reflection.hpp>

#include <net/neighbourhood.hpp> // using Neighbourhood::MaxNeighbours constant

namespace po = boost::program_options;
namespace ip = boost::asio::ip;

using NodeVersion = cs::Version;
extern const NodeVersion NODE_VERSION;

const std::string DEFAULT_PATH_TO_CONFIG = "config.ini";
const std::string DEFAULT_PATH_TO_DB = "db";
const std::string DEFAULT_PATH_TO_KEY = "keys.dat";

const std::string DEFAULT_PATH_TO_PUBLIC_KEY = "NodePublic.txt";
const std::string DEFAULT_PATH_TO_PRIVATE_KEY = "NodePrivate.txt";

const uint32_t DEFAULT_MIN_NEIGHBOURS = 5;
const uint32_t DEFAULT_MAX_NEIGHBOURS = Neighbourhood::MaxNeighbours;
const uint32_t DEFAULT_CONNECTION_BANDWIDTH = 1 << 19;
const uint32_t DEFAULT_OBSERVER_WAIT_TIME = 5 * 60 * 1000;  // ms
const uint32_t DEFAULT_ROUND_ELAPSE_TIME = 1000 * 60; // ms

const size_t DEFAULT_CONVEYER_SEND_CACHE_VALUE = 10;             // rounds
const size_t DEFAULT_CONVEYER_MAX_RESENDS_SEND_CACHE = 10;       // retries

[[maybe_unused]]
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
    int executorRunDelay = 100;
    int executorBackgroundThreadDelay = 100;
    int executorCheckVersionDelay = 1000;
    bool executorMultiInstance = false;
    int executorCommitMin = 1506;   // first commit with support of checking
    int executorCommitMax{-1};      // unlimited range on the right
    std::string jpsCmdLine = "jps";
};

struct ConveyerData {
    size_t sendCacheValue = DEFAULT_CONVEYER_SEND_CACHE_VALUE;
    size_t maxResendsSendCache = DEFAULT_CONVEYER_MAX_RESENDS_SEND_CACHE;
};

struct EventsReportData {
    // event reports collector address
    EndpointData collector_ep;

    // general on/off
    bool on = false;

    // report filters, only actual if on is true
    
    // report every liar in consensus
    bool consensus_liar = false;
    // report every silent trusted node in consensus
    bool consensus_silent = false;
    // report consensus is not achieved
    bool consensus_failed = true;
    // report every liar in smart contracts consensus
    bool contracts_liar = false;
    // report every silent trusted node in smart contracts consensus
    bool contracts_silent = false;
    // report smart contracts consensus is not achieved
    bool contracts_failed = true;
    // report put node into gray list
    bool add_to_gray_list = true;
    // report remove node from gray list
    bool erase_from_gray_list = false;
    // basic transaction is rejected by final consensus
    bool reject_transaction = true;
    // contract-related transaction is rejected just after execution, before consensus started
    bool reject_contract_execution = true;
    // contract-related transaction is rejected by final basic consensus
    bool reject_contract_consensus = true;
    // invalid block detected by node
    bool alarm_invalid_block = true;
    // big bang occurred
    bool big_bang = false;
};

class Config {
public:
    Config() = default;

    explicit Config(const ConveyerData& conveyerData);

    Config(const Config&) = default;
    Config(Config&&) = default;
    Config& operator=(const Config&) = default;
    Config& operator=(Config&&) = default;

    static Config read(po::variables_map&);
    
    template<typename T, typename ... Ts, typename = cs::IsConvertToString<T, Ts...>>
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

    uint32_t getMinNeighbours() const {
        return minNeighbours_;
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

    bool autoShutdownEnabled() const {
        return autoShutdownEnabled_;
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

    NodeVersion getMinCompatibleVersion() const {
        return minCompatibleVersion_;
    }

    void dumpJSONKeys(const std::string& fName) const;

    bool alwaysExecuteContracts() const {
        return alwaysExecuteContracts_;
    }

    bool newBlockchainTop() const {
        return newBlockchainTop_;
    }

    bool isCompatibleVersion() const {
        return compatibleVersion_;
    }

    uint64_t newBlockchainTopSeq() const {
        return newBlockchainTopSeq_;
    }

    uint64_t observerWaitTime() const {
        return observerWaitTime_;
    }

    uint64_t roundElapseTime() const {
        return roundElapseTime_;
    }

    bool readKeys(const po::variables_map& vm);
    bool enterWithSeed();

    const ConveyerData& conveyerData() const {
        return conveyerData_;
    }

    void swap(Config& config);

    const EventsReportData& getEventsReportData() const {
        return eventsReport_;
    }

private:
    static Config readFromFile(const std::string& fileName);

    void setLoggerSettings(const boost::property_tree::ptree& config);
    void readPoolSynchronizerData(const boost::property_tree::ptree& config);
    void readApiData(const boost::property_tree::ptree& config);
    void readConveyerData(const boost::property_tree::ptree& config);
    void readEventsReportData(const boost::property_tree::ptree& config);

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
    NodeVersion minCompatibleVersion_ = NODE_VERSION;

    bool ipv6_ = false;

    uint32_t minNeighbours_ = DEFAULT_MIN_NEIGHBOURS;
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
    bool newBlockchainTop_ = false;
    bool autoShutdownEnabled_ = true;
    bool compatibleVersion_ = false;
    uint64_t newBlockchainTopSeq_;

    uint64_t observerWaitTime_ = DEFAULT_OBSERVER_WAIT_TIME;
    uint64_t roundElapseTime_ = DEFAULT_ROUND_ELAPSE_TIME;

    ConveyerData conveyerData_;

    EventsReportData eventsReport_;

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
        const auto endPos = (tmpPos != std::string::npos ? tmpPos - 1 : newConfig.size());
        newConfig.replace(startPos, endPos, fullReplaceString);
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
