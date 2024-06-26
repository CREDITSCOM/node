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
extern const uint8_t MINOR_NODE_VERSION;

const std::string DEFAULT_PATH_TO_CONFIG = "config.ini";
const std::string DEFAULT_PATH_TO_DB = "db";
const std::string DEFAULT_PATH_TO_KEY = "keys.dat";

const std::string DEFAULT_PATH_TO_PUBLIC_KEY = "NodePublic.txt";
const std::string DEFAULT_PATH_TO_PRIVATE_KEY = "NodePrivate.txt";

const uint32_t DEFAULT_MIN_NEIGHBOURS = Neighbourhood::kMinNeighbours;
const uint32_t DEFAULT_MAX_NEIGHBOURS = Neighbourhood::kMaxNeighbours;
const uint64_t DEFAULT_MAX_UNCORRECTED_BLOCK = 0;
const uint32_t DEFAULT_CONNECTION_BANDWIDTH = 1 << 19;
const uint32_t DEFAULT_OBSERVER_WAIT_TIME = 5 * 60 * 1000;  // ms
const uint32_t DEFAULT_ROUND_ELAPSE_TIME = 1000 * 60; // ms
const uint32_t DEFAULT_STORE_BLOCK_ELAPSE_TIME = 1000 * 40; // ms

const size_t DEFAULT_CONVEYER_MAX_PACKET_LIFETIME = 30; // rounds

using Port = short unsigned;

struct EndpointData {
    bool ipSpecified = false;

    std::string id;
    std::string ip;
    short unsigned port = 0;

    static EndpointData fromString(const std::string&);
};

enum BootstrapType {
    IpList
};

struct PoolSyncData {
    cs::Sequence blockPoolsCount = 100;              // max block count in one request: cannot be 0
    uint16_t sequencesVerificationFrequency = 350;   // sequences received verification frequency : 0-never; 1-once per round: other- in ms;
};

struct ApiData {
    // on by default:
    uint16_t port = 9090;
    // off by default:
    uint16_t ajaxPort = 0; // former 8081;
    // on by default:
    uint16_t executorPort = 9080;
    // on by default:
    uint16_t apiexecPort = 9070;
    // off by default:
    uint16_t diagPort = 0; // former 9060;
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
    int executorCheckVersionDelay = 5000;
    bool executorMultiInstance = false;
    int executorCommitMin = 1506;   // first commit with support of checking
    int executorCommitMax{-1};      // unlimited range on the right
    std::string jpsCmdLine = "jps";
};

struct ConveyerData {
    size_t maxPacketLifeTime = DEFAULT_CONVEYER_MAX_PACKET_LIFETIME;
};

struct EventsReportData {
    // event reports collector address
    std::string collector_id;

    // general on/off
    bool is_active = false;

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
};

struct DbSQLData {
    // SQL server host name or ip address
    std::string host { "localhost" };
    // connection port 5432 by default
    int port = 5432;
    // name of database
    std::string name { "roundinfo" };
    // username and password for access
    std::string user { "postgres" };
    std::string password { "postgres" };
};

namespace cs::config {
class Observer;
}

class Config {
public:
    Config() = default;

    explicit Config(const ConveyerData& conveyerData);

    Config(const Config&) = default;
    Config(Config&&) = default;
    Config& operator=(const Config&) = default;
    Config& operator=(Config&&) = default;

    static Config read(po::variables_map&, bool);
    
    const EndpointData& getInputEndpoint() const {
        return inputEp_;
    }

    const EndpointData& getOutputEndpoint() const {
        return outputEp_;
    }

    const std::vector<EndpointData>& getIpList() const {
        return bList_;
    }

    const std::vector<cs::PublicKey>& getInitialConfidants() const {
        return initialConfidants_;
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
    
    static NodeVersion getMinorNodeVersion() {
        return MINOR_NODE_VERSION;
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

    bool getForkGeneration() const{
        return generateFork_;
    }


    bool isSyncOn() const {
        return sync_on_;
    }

    bool isIdleMode() const {
        return idleMode_;
    }

    uint64_t maxUncorrectedBlock() const {
        return maxUncorrectedBlock_;
    }

    bool traverseNAT() const {
        return traverseNAT_;
    }

    bool daemonMode() const {
        return daemonMode_;
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

    uint64_t storeBlockElapseTime() const {
        return storeBlockElapseTime_;
    }

    bool readKeys(const po::variables_map& vm);
    bool enterWithSeed();

    const ConveyerData& conveyerData() const {
        return conveyerData_;
    }

    void swap(Config& config);

    void updateKnownHosts(std::vector<cs::PeerData>&) const;

    const EventsReportData& getEventsReportData() const {
        return eventsReport_;
    }

    const DbSQLData& getDbSQLData() const {
        return dbSQLData_;
    }

    bool isStakinOn(cs::RoundNumber round);
    bool isMiningOn(cs::RoundNumber round);

    bool getBalanceChangeFlag() const {
        return showBalanceChange_;
    }

    cs::PublicKey getBalanceChangeKey() const{
        return showBalanceChangeKey_;
    }

    std::string getBalanceChangeAddress() const {
        return showBalanceChangeAddress_;
        ;
    }


private:
    static Config readFromFile(const std::string& fileName);

    void setLoggerSettings(const boost::property_tree::ptree& config);
    void readPoolSynchronizerData(const boost::property_tree::ptree& config);
    void readApiData(const boost::property_tree::ptree& config);
    void readConveyerData(const boost::property_tree::ptree& config);
    void readEventsReportData(const boost::property_tree::ptree& config);
    void readDbSQLData(const boost::property_tree::ptree& config);

    bool readKeys(const std::string& pathToPk, const std::string& pathToSk, const bool encrypt);
    void showKeys(const std::string& pk58);

    void changePasswordOption(const std::string& pathToSk);

    template <typename T>
    bool checkAndSaveValue(const boost::property_tree::ptree& data, const std::string& block, const std::string& param, T& value);
    bool checkAndSaveValue(const boost::property_tree::ptree& data, const std::string& block, const std::string& param, std::string& value);

    bool good_ = false;

    EndpointData inputEp_;

    bool twoSockets_ = false;

    EndpointData outputEp_;

    NodeVersion minCompatibleVersion_ = NODE_VERSION;

    bool ipv6_ = false;

    uint32_t minNeighbours_ = DEFAULT_MIN_NEIGHBOURS;
    uint32_t maxNeighbours_ = DEFAULT_MAX_NEIGHBOURS;
    bool restrictNeighbours_ = false;
    uint64_t connectionBandwidth_ = DEFAULT_CONNECTION_BANDWIDTH;
    uint64_t maxUncorrectedBlock_ = DEFAULT_MAX_UNCORRECTED_BLOCK;
    bool symmetric_ = false;

    EndpointData hostAddressEp_;

    std::vector<EndpointData> bList_;
    std::vector<cs::PublicKey> initialConfidants_;

    std::string pathToDb_;
    std::string hostsFileName_;

    cs::PublicKey publicKey_{};
    cs::PrivateKey privateKey_{};

    boost::log::settings loggerSettings_{};

    PoolSyncData poolSyncData_;
    ApiData apiData_;
    DbSQLData dbSQLData_;

    bool showBalanceChange_ = false;
    bool alwaysExecuteContracts_ = false;
    bool recreateIndex_ = false;
    bool newBlockchainTop_ = false;
    bool autoShutdownEnabled_ = true;
    bool compatibleVersion_ = true;
    bool traverseNAT_ = true;
    bool sync_on_ = true;
    uint64_t newBlockchainTopSeq_;
    bool generateFork_ = false;
    bool idleMode_ = false;

    bool daemonMode_ = false;

    uint64_t observerWaitTime_ = DEFAULT_OBSERVER_WAIT_TIME;
    uint64_t roundElapseTime_ = DEFAULT_ROUND_ELAPSE_TIME;
    uint64_t storeBlockElapseTime_ = DEFAULT_STORE_BLOCK_ELAPSE_TIME;

    ConveyerData conveyerData_;

    EventsReportData eventsReport_;

    cs::PublicKey showBalanceChangeKey_;
    std::string showBalanceChangeAddress_;

    std::vector<std::pair<cs::RoundNumber, cs::RoundNumber>> stakingRoundRanges_;

    std::vector<std::pair<cs::RoundNumber, cs::RoundNumber>> miningRoundRanges_;

    friend bool operator==(const Config&, const Config&);
    friend class cs::config::Observer;
};

// all operators
bool operator==(const EndpointData& lhs, const EndpointData& rhs);
bool operator!=(const EndpointData& lhs, const EndpointData& rhs);

bool operator==(const PoolSyncData& lhs, const PoolSyncData& rhs);
bool operator!=(const PoolSyncData& lhs, const PoolSyncData& rhs);

bool operator==(const ApiData& lhs, const ApiData& rhs);
bool operator!=(const ApiData& lhs, const ApiData& rhs);

bool operator==(const DbSQLData& lhs, const DbSQLData& rhs);
bool operator!=(const DbSQLData& lhs, const DbSQLData& rhs);

bool operator==(const ConveyerData& lhs, const ConveyerData& rhs);
bool operator!=(const ConveyerData& lhs, const ConveyerData& rhs);

bool operator==(const Config& lhs, const Config& rhs);
bool operator!=(const Config& lhs, const Config& rhs);

#endif  // CONFIG_HPP
