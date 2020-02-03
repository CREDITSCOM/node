#ifndef NODE_HPP
#define NODE_HPP

#include <iostream>
#include <memory>
#include <string>

#include <csstats.hpp>

#include <csconnector/csconnector.hpp>

#include <csnode/conveyer.hpp>
#include <csnode/compressor.hpp>

#include <lib/system/timer.hpp>

#include <net/neighbourhood.hpp>

#include "blockchain.hpp"
#include "confirmationlist.hpp"
#include "roundstat.hpp"

class Transport;

namespace cs {
class SolverCore;
}

namespace cs {
class PoolSynchronizer;
class BlockValidator;
}  // namespace cs

namespace cs::config {
class Observer;
}

namespace cs {
class RoundPackage;
}

class Node {
public:
    enum Level {
        Normal,
        Confidant,
        Main,
        Writer
    };

    enum Orders {
        Release,
        Seal
    };

    enum MessageActions {
        Process,
        Postpone,
        Drop
    };

    using RefExecution = std::pair<cs::Sequence, uint32_t>;

    explicit Node(cs::config::Observer& observer);
    ~Node();

    bool isGood() const {
        return good_;
    }

    void run();
    void stop();
    void destroy();

    static void requestStop();

    bool isStopRequested() const {
        return stopRequested_;
    }

    std::string getSenderText(const cs::PublicKey& sender);

    // incoming requests processing
    void getBootstrapTable(const uint8_t* data, const size_t size, const cs::RoundNumber);
    bool verifyPacketSignatures(cs::TransactionsPacket& packet, const cs::PublicKey& sender);
    bool verifyPacketTransactions(cs::TransactionsPacket packet, const cs::PublicKey& sender);
    void getTransactionsPacket(const uint8_t* data, const std::size_t size, const cs::PublicKey& sender);
    void getNodeStopRequest(const cs::RoundNumber round, const uint8_t* data, const std::size_t size);

    void addToBlackListCounter(const cs::PublicKey& key);
    void updateBlackListCounter();
    // critical is true if network near to be down, all capable trusted node required
    bool canBeTrusted(bool critical);

    // SOLVER3 methods
    void getRoundTable(const uint8_t* data, const size_t size, const cs::RoundNumber, const cs::PublicKey& sender);
    void setCurrentRP(const cs::RoundPackage& rp);
    void performRoundPackage(cs::RoundPackage& rPackage, const cs::PublicKey& sender, bool updateRound);
    bool isTransactionsInputAvailable();
    void clearRPCache(cs::RoundNumber rNum);
    void sendHash(cs::RoundNumber round);
    void getHash(const uint8_t* data, const size_t size, cs::RoundNumber rNum, const cs::PublicKey& sender);
    void roundPackRequest(const cs::PublicKey& respondent, cs::RoundNumber round);
    void askConfidantsRound(cs::RoundNumber round, const cs::ConfidantsKeys& confidants);
    void getRoundPackRequest(const uint8_t* data, const size_t size, cs::RoundNumber rNum, const cs::PublicKey& sender);
    void emptyRoundPackReply(const cs::PublicKey & respondent);
    void getEmptyRoundPack(const uint8_t * data, const size_t size, cs::RoundNumber rNum, const cs::PublicKey & sender);
    void roundPackReply(const cs::PublicKey& respondent);
    void sendHashReply(const csdb::PoolHash& hash, const cs::PublicKey& respondent);
    void getHashReply(const uint8_t* data, const size_t size, cs::RoundNumber rNum, const cs::PublicKey& sender);

    // consensus communication
    void sendStageOne(const cs::StageOne&);
    void sendStageTwo(cs::StageTwo&);
    void sendStageThree(cs::StageThree&);

    void getStageOne(const uint8_t* data, const size_t size, const cs::PublicKey& sender);
    void getStageTwo(const uint8_t* data, const size_t size, const cs::PublicKey& sender);
    void getStageThree(const uint8_t* data, const size_t size, const cs::PublicKey& sender);

    void adjustStageThreeStorage();
    void stageRequest(MsgTypes msgType, uint8_t respondent, uint8_t required, uint8_t iteration);
    void getStageRequest(const MsgTypes msgType, const uint8_t* data, const size_t size, const cs::PublicKey& requester);
    void sendStageReply(const uint8_t sender, const cs::Signature& signature, const MsgTypes msgType, const uint8_t requester, cs::Bytes& message);

    // smart-contracts consensus communicatioin
    void sendSmartStageOne(const cs::ConfidantsKeys& smartConfidants, const cs::StageOneSmarts& stageOneInfo);
    void getSmartStageOne(const uint8_t* data, const size_t size, const cs::RoundNumber rNum, const cs::PublicKey& sender);
    void sendSmartStageTwo(const cs::ConfidantsKeys& smartConfidants, cs::StageTwoSmarts& stageTwoInfo);
    void getSmartStageTwo(const uint8_t* data, const size_t size, const cs::RoundNumber rNum, const cs::PublicKey& sender);
    void sendSmartStageThree(const cs::ConfidantsKeys& smartConfidants, cs::StageThreeSmarts& stageThreeInfo);
    void getSmartStageThree(const uint8_t* data, const size_t size, const cs::RoundNumber rNum, const cs::PublicKey& sender);
    void smartStageEmptyReply(uint8_t requesterNumber);
    bool smartStageRequest(MsgTypes msgType, uint64_t smartID, const cs::PublicKey& confidant, uint8_t respondent, uint8_t required);
    void getSmartStageRequest(const MsgTypes msgType, const uint8_t* data, const size_t size, const cs::PublicKey& requester);
    void sendSmartStageReply(const cs::Bytes& message, const cs::Signature& signature, const MsgTypes msgType, const cs::PublicKey& requester);

    void addSmartConsensus(uint64_t id);
    void removeSmartConsensus(uint64_t id);
    void checkForSavedSmartStages(uint64_t id);

    void sendSmartReject(const std::vector<RefExecution>& rejectList);
    void getSmartReject(const uint8_t* data, const size_t size, const cs::RoundNumber rNum, const cs::PublicKey& sender);

    csdb::PoolHash spoileHash(const csdb::PoolHash& hashToSpoil);
    csdb::PoolHash spoileHash(const csdb::PoolHash& hashToSpoil, const cs::PublicKey& pKey);

    cs::ConfidantsKeys retriveSmartConfidants(const cs::Sequence startSmartRoundNumber) const;

    void onRoundStart(const cs::RoundTable& roundTable, bool updateRound);
    void startConsensus();

    void prepareRoundTable(cs::RoundTable& roundTable, const cs::PoolMetaInfo& poolMetaInfo, cs::StageThree& st3);
    bool receivingSignatures(cs::RoundPackage& rPackage, cs::PublicKeys& currentConfidants);
    bool rpSpeedOk(cs::RoundPackage& rPackage);
    bool isLastRPStakeFull(cs::RoundNumber rNum);
    void addRoundSignature(const cs::StageThree& st3);
    // smart-contracts consensus stages sending and getting

    // send request for next round info from trusted node specified by index in list
    void sendRoundTableRequest(uint8_t respondent);

    // send request for next round info from node specified node
    void sendRoundTableRequest(const cs::PublicKey& respondent);
    void getRoundTableRequest(const uint8_t*, const size_t, const cs::RoundNumber, const cs::PublicKey&);
    void sendRoundTableReply(const cs::PublicKey& target, bool hasRequestedInfo);
    void getRoundTableReply(const uint8_t* data, const size_t size, const cs::PublicKey& respondent);

    // called by solver, review required:
    bool tryResendRoundTable(const cs::PublicKey& target, const cs::RoundNumber rNum);
    void sendRoundTable(cs::RoundPackage& rPackage);

    // transaction's pack syncro
    void getPacketHashesRequest(const uint8_t*, const std::size_t, const cs::RoundNumber, const cs::PublicKey&);
    void getPacketHashesReply(const uint8_t*, const std::size_t, const cs::RoundNumber, const cs::PublicKey& sender);
    void getBlockAlarm(const uint8_t*, const std::size_t, const cs::RoundNumber, const cs::PublicKey& sender);
    void getEventReport(const uint8_t*, const std::size_t, const cs::RoundNumber, const cs::PublicKey& sender);

    bool checkCharacteristic(cs::RoundPackage& rPackage);
    void getCharacteristic(cs::RoundPackage& rPackage);

    void sendBlockAlarm(const cs::PublicKey& source_node, cs::Sequence seq);

    void cleanConfirmationList(cs::RoundNumber rNum);

    // state syncro functions
    
    void sendStateRequest(const csdb::Address& contract_abs_addr, const cs::PublicKeys& confidants);
    void getStateRequest(const uint8_t*, const std::size_t, const cs::RoundNumber, const cs::PublicKey& sender);
    void sendStateReply(const cs::PublicKey& respondent, const csdb::Address& contract_abs_addr, const cs::Bytes& data);
    void getStateReply(const uint8_t*, const std::size_t, const cs::RoundNumber, const cs::PublicKey& sender);

    // syncro get functions
    void getBlockRequest(const uint8_t*, const size_t, const cs::PublicKey& sender);
    void getBlockReply(const uint8_t*, const size_t);

    // transaction's pack syncro
    void sendTransactionsPacket(const cs::TransactionsPacket& packet);
    void sendPacketHashesRequest(const cs::PacketsHashes& hashes, const cs::RoundNumber round, uint32_t requestStep);
    void sendPacketHashesReply(const cs::PacketsVector& packets, const cs::RoundNumber round, const cs::PublicKey& target);

    // smarts consensus additional functions:

    // syncro send functions
    void sendBlockReply(const cs::PoolsBlock& poolsBlock, const cs::PublicKey& target);

    /**
     * Initializes the default round package as containing the default round table (default trusted
     * nodes)
     *
     * @author  Alexander Avramenko
     * @date    04.12.2019
     *
     * @param   confidants  The actual confidants set.
     */

    void initBootstrapRP(const std::set<cs::PublicKey>& confidants);
    bool isBootstrapRound() const {
        return isBootstrapRound_;
    }
    void getUtilityMessage(const uint8_t* data, const size_t size);
    void becomeWriter();

    bool isPoolsSyncroStarted();

    std::optional<cs::TrustedConfirmation> getConfirmation(cs::RoundNumber round) const;

    // this function should filter the packages only using their roundNumber
    MessageActions chooseMessageAction(const cs::RoundNumber, const MsgTypes, const cs::PublicKey);

    void updateConfigFromFile();

    // @TODO: remove this function
    bool isBlackListed(const cs::PublicKey) { return false; }

    const cs::PublicKey& getNodeIdKey() const {
        return nodeIdKey_;
    }

    Level getNodeLevel() const {
        return myLevel_;
    }

    uint8_t getConfidantNumber() const {
        return myConfidantIndex_;
    }

    uint8_t subRound() const {
        return subRound_;
    }

    BlockChain& getBlockChain() {
        return blockChain_;
    }

    const BlockChain& getBlockChain() const {
        return blockChain_;
    }

    cs::SolverCore* getSolver() {
        return solver_;
    }

    const cs::SolverCore* getSolver() const {
        return solver_;
    }

#ifdef NODE_API
    csconnector::connector* getConnector() {
        return api_.get();
    }
#endif

    template <typename T>
    using SmartsSignal = cs::Signal<void(T&, bool)>;
    using SmartStageRequestSignal = cs::Signal<void(uint8_t, uint64_t, uint8_t, uint8_t, const cs::PublicKey&)>;
    using StopSignal = cs::Signal<void()>;

    // args: [failed list, restart list]
    using RejectedSmartContractsSignal = cs::Signal<void(const std::vector<RefExecution>&)>;

    void reportEvent(const cs::Bytes& bin_pack);

public signals:
    SmartsSignal<cs::StageOneSmarts> gotSmartStageOne;
    SmartsSignal<cs::StageTwoSmarts> gotSmartStageTwo;
    SmartsSignal<cs::StageThreeSmarts> gotSmartStageThree;
    SmartStageRequestSignal receivedSmartStageRequest;
    RejectedSmartContractsSignal gotRejectedContracts;

    inline static StopSignal stopRequested;

private slots:
    void onStopRequested();

public slots:
    void processTimer();
    void onTransactionsPacketFlushed(const cs::TransactionsPacket& packet);
    void onPingChecked(cs::Sequence sequence, const cs::PublicKey& sender);
    void sendBlockRequest(const cs::PublicKey& target, const cs::PoolsRequestedSequences& sequences);
    void processSpecialInfo(const csdb::Pool& pool);
    void validateBlock(const csdb::Pool& block, bool* shouldStop);
    void deepBlockValidation(csdb::Pool block, bool* shouldStop);
    void sendBlockAlarmSignal(cs::Sequence seq);
    void onRoundTimeElapsed();
    void onNeighbourAdded(const cs::PublicKey& neighbour, cs::Sequence lastSeq, cs::RoundNumber lastRound);
    void onNeighbourRemoved(const cs::PublicKey& neighbour);

private:
    bool init();
    void initPoolSynchronizer();

    void setupNextMessageBehaviour();
    void setupPoolSynchronizerBehaviour();

    bool sendRoundPackage(const cs::RoundNumber rNum, const cs::PublicKey& target);
    void sendRoundPackageToAll(cs::RoundPackage& rPackage);

    void reviewConveyerHashes();

    void processSync();

    // transport
    void addToBlackList(const cs::PublicKey& key, bool isMarked);

    // conveyer
    void processPacketsRequest(cs::PacketsHashes&& hashes, const cs::RoundNumber round, const cs::PublicKey& sender);
    void processPacketsReply(cs::PacketsVector&& packets, const cs::RoundNumber round);
    void processTransactionsPacket(cs::TransactionsPacket&& packet);

    /// sending interace methods

    template <typename... Args>
    void sendDirect(const cs::PublicKey& target, const MsgTypes msgType, const cs::RoundNumber round, Args&&... args);

    template <typename... Args>
    void sendDirect(const cs::PublicKeys& keys, const MsgTypes msgType, const cs::RoundNumber round, Args&&... args);

    template <class... Args>
    void sendBroadcast(const MsgTypes msgType, const cs::RoundNumber round, Args&&... args);

    template <class... Args>
    void sendBroadcastIfNoConnection(const cs::PublicKey& target, const MsgTypes msgType, const cs::RoundNumber round, Args&&... args);

    // to current confidants list
    template <class... Args>
    void sendConfidants(const MsgTypes msgType, const cs::RoundNumber round, Args&&... args);

    // TODO: C++ 17 static inline?
    static const csdb::Address genesisAddress_;
    static const csdb::Address startAddress_;

    const cs::PublicKey nodeIdKey_;
    const cs::PrivateKey nodeIdPrivate_;
    bool good_ = true;

    std::atomic_bool stopRequested_{ false };

    // file names for crypto public/private keys
    inline const static std::string privateKeyFileName_ = "NodePrivate.txt";
    inline const static std::string publicKeyFileName_ = "NodePublic.txt";

    Level myLevel_{Level::Normal};
    cs::Byte myConfidantIndex_{cs::ConfidantConsts::InvalidConfidantIndex};

    // main cs storage
    BlockChain blockChain_;

    // appidional dependencies
    cs::SolverCore* solver_;
    Transport* transport_;

#ifdef NODE_API
    std::unique_ptr<csconnector::connector> api_;
#endif

    uint32_t startPacketRequestPoint_ = 0;

    // ms timeout
    static const uint32_t packetRequestStep_ = 450;
    static const size_t maxPacketRequestSize_ = 1000;
    static const size_t kLastPoolSynchroDelay_ = 30000;

    cs::PoolSynchronizer* poolSynchronizer_;

    // sends transactions blocks to network
    cs::Timer sendingTimer_;
    cs::Byte subRound_{0};

    // round package sent data storage
    struct SentRoundData {
        cs::RoundTable table;
        cs::Byte subRound{0};
    };

    struct SentSignatures {
        cs::Signatures poolSignatures;
        cs::Signatures roundSignatures;
        cs::Signatures trustedConfirmation;
    };

    cs::Bytes lastRoundTableMessage_;
    cs::Bytes lastSignaturesMessage_;

    std::vector<cs::Bytes> stageOneMessage_;
    std::vector<cs::Bytes> stageTwoMessage_;
    std::vector<cs::Bytes> stageThreeMessage_;
    bool stageThreeSent_ = false;

    std::vector<cs::Bytes> smartStageOneMessage_;
    std::vector<cs::Bytes> smartStageTwoMessage_;
    std::vector<cs::Bytes> smartStageThreeMessage_;

    std::vector<cs::StageOneSmarts> smartStageOneStorage_;
    std::vector<cs::StageTwoSmarts> smartStageTwoStorage_;
    std::vector<cs::StageThreeSmarts> smartStageThreeStorage_;

    // smart consensus IDs:
    std::vector<uint64_t> activeSmartConsensuses_;

    SentRoundData lastSentRoundData_;
    SentSignatures lastSentSignatures_;

    std::vector<bool> badHashReplyCounter_;

    // round stat
    cs::RoundStat stat_;

    // confirmation list
    cs::ConfirmationList confirmationList_;
    cs::RoundTableMessage currentRoundTableMessage_;

    //expected rounds
    std::vector<cs::RoundNumber> expectedRounds_;
    cs::Bytes lastTrustedMask_;

    std::unique_ptr<cs::BlockValidator> blockValidator_;
    std::vector<cs::RoundPackage> roundPackageCache_;

    cs::RoundPackage currentRoundPackage_;
    size_t roundPackRequests_ = 0;
    bool lastBlockRemoved_ = false;
    std::map<cs::RoundNumber, uint8_t> receivedBangs;
    std::map<cs::PublicKey, size_t> blackListCounter_;
    size_t lastRoundPackageTime_ = 0;

    cs::config::Observer& observer_;
    cs::Compressor compressor_;

    std::string kLogPrefix_;
    std::map<uint16_t, cs::Command> changeableParams_;

    std::set<cs::PublicKey> initialConfidants_;
    bool isBootstrapRound_ = false;
};

std::ostream& operator<<(std::ostream& os, Node::Level nodeLevel);

#endif  // NODE_HPP
