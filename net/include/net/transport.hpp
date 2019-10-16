/* Send blaming letters to @yrtimd */
#ifndef TRANSPORT_HPP
#define TRANSPORT_HPP

#include <boost/asio.hpp>
#include <csignal>
#include <atomic>

#include <config.hpp>

#include <csnode/packstream.hpp>

#include <lib/system/allocators.hpp>
#include <lib/system/cache.hpp>
#include <lib/system/common.hpp>
#include <lib/system/logger.hpp>
#include <lib/system/signals.hpp>
#include <lib/system/lockfreechanger.hpp>

#include <net/network.hpp>

#include "neighbourhood.hpp"
#include "packet.hpp"
#include "pacmans.hpp"

inline volatile std::sig_atomic_t gSignalStatus = 0;

using ConnectionId = uint64_t;
using Tick = uint64_t;

using PingSignal = cs::Signal<void(cs::Sequence, const cs::PublicKey&)>;

enum class NetworkCommand : uint8_t {
    Registration = 2,
    ConfirmationRequest,
    ConfirmationResponse,
    RegistrationConfirmed,
    RegistrationRefused,
    Ping,
    PackInform,
    PackRequest,
    PackRenounce,
    BlockSyncRequest,
    SSRegistration = 1,
    SSFirstRound = 31,
    SSRegistrationRefused = 25,
    SSPingWhiteNode = 32,
    SSLastBlock = 34,
    SSReRegistration = 36,
    SSSpecificBlock = 37,
    SSNewFriends = 38,
    SSUpdateServer = 39,
    IntroduceConsensus = 40,
    IntroduceConsensusReply = 41
};

enum class RegistrationRefuseReasons : uint8_t {
    Unspecified,
    LimitReached,
    BadId,
    BadClientVersion,
    Timeout,
    BadResponse,
    IncompatibleBlockchain
};

enum class SSBootstrapStatus : uint8_t {
    Empty,
    Requested,
    RegisteredWait,
    Complete,
    Denied
};

template <>
uint16_t getHashIndex(const ip::udp::endpoint&);

class Node;

class Transport {
public:
    explicit Transport(const Config& config, Node* node);
    ~Transport();

    void run();

    inline static volatile std::sig_atomic_t gSignalStatus = 0;

    static void stop() {
        Transport::gSignalStatus = 1;
    }

    static const char* networkCommandToString(NetworkCommand command);

    RemoteNodePtr getPackSenderEntry(const ip::udp::endpoint&);

    void processNetworkTask(const TaskPtr<IPacMan>&, RemoteNodePtr&);
    void processNodeMessage(const Message&);
    void processNodeMessage(const Packet&);

    const cs::PublicKey& getMyPublicKey() const {
        return myPublicKey_;
    }

    bool isGood() const {
        return good_;
    }

    bool isOwnNodeTrusted() const;

    void sendBroadcast(const Packet* pack) {
        nh_.sendByNeighbours(pack);
    }

    bool sendDirect(const Packet*, const Connection&);
    bool sendDirectToSock(Packet*, const Connection&);
    void deliverDirect(const Packet*, const uint32_t, ConnectionPtr);
    void deliverBroadcast(const Packet*, const uint32_t);
    void deliverConfidants(const Packet* pack, const uint32_t size);
    void deliverConfidants(const Packet* pack, const uint32_t size, const std::vector<cs::PublicKey>&, int except = -1);
    bool checkConfidants(const std::vector<cs::PublicKey>& list, int except = -1);
    bool isConfidants();
    void removeConfidants();

    void gotPacket(const Packet&, RemoteNodePtr&);
    void redirectPacket(const Packet&, RemoteNodePtr&, bool resend = true);
    bool shouldSendPacket(const Packet&);

    void refillNeighbourhood();
    void processPostponed(const cs::RoundNumber);

    void sendRegistrationRequest(Connection&);
    void sendRegistrationConfirmation(const Connection&, const Connection::Id);
    void sendRegistrationRefusal(const Connection&, const RegistrationRefuseReasons);
    void sendPackRenounce(const cs::Hash&, const Connection&);
    void sendPackInform(const Packet&, const Connection&);
    void sendPackInform(const Packet& pack, RemoteNodePtr&);
    void sendSSIntroduceConsensus(const std::vector<cs::PublicKey>& keys);

    void sendPingPack(const Connection&);

    void registerMessage(MessagePtr);

    // neighbours interface
    uint32_t getNeighboursCount();
    uint32_t getNeighboursCountWithoutSS();
    uint32_t getMaxNeighbours() const;
    ConnectionPtr getConnectionByKey(const cs::PublicKey& pk);
    ConnectionPtr getConnectionByNumber(const std::size_t number);
    ConnectionPtr getRandomNeighbour();
    cs::Sequence getConnectionLastSequence(const std::size_t number);

    auto getNeighboursLock() const {
        return nh_.getNeighboursLock();
    }

    // thread safe negihbours methods
    void forEachNeighbour(std::function<void(ConnectionPtr)> func);
    void forEachNeighbourWithoudSS(std::function<void(ConnectionPtr)> func);
    bool forRandomNeighbour(std::function<void(ConnectionPtr)> func);

    // no thread safe
    const Connections getNeighbours() const;
    const Connections getNeighboursWithoutSS() const;

    bool isPingDone();
    void resetNeighbours();

public signals:
    PingSignal pingReceived;
    cs::Action mainThreadIterated;

public slots:
    void onConfigChanged(const Config& updated);

private:
    void addMyOut(const uint8_t initFlagValue = 0);
    void formRegPack(uint64_t** regPackConnId, const cs::PublicKey& pk, uint64_t uuid);
    void formSSConnectPack(const cs::PublicKey& pk, uint64_t uuid);

    void registerTask(Packet* pack, const uint32_t packNum, const bool);
    void postponePacket(const cs::RoundNumber, const MsgTypes, const Packet&);

    // Dealing with network connections
    bool parseSSSignal(const TaskPtr<IPacMan>&);

    void dispatchNodeMessage(const MsgTypes, const cs::RoundNumber, const Packet&, const uint8_t* data, size_t);

    /* Network packages processing */
    bool gotRegistrationRequest(const TaskPtr<IPacMan>&, RemoteNodePtr&);

    bool gotRegistrationConfirmation(const TaskPtr<IPacMan>&, RemoteNodePtr&);

    bool gotRegistrationRefusal(const TaskPtr<IPacMan>&, RemoteNodePtr&);

    bool gotSSRegistration(const TaskPtr<IPacMan>&, RemoteNodePtr&);
    bool gotSSReRegistration();
    bool gotSSRefusal(const TaskPtr<IPacMan>&);
    bool gotSSDispatch(const TaskPtr<IPacMan>&);
    bool gotSSPingWhiteNode(const TaskPtr<IPacMan>&);
    bool gotSSLastBlock(const TaskPtr<IPacMan>&, cs::Sequence, const csdb::PoolHash&, bool canBeTrusted);
    bool gotSSNewFriends();
    bool gotSSUpdateServer();

    bool gotPackInform(const TaskPtr<IPacMan>&, RemoteNodePtr&);
    bool gotPackRenounce(const TaskPtr<IPacMan>&, RemoteNodePtr&);
    bool gotPackRequest(const TaskPtr<IPacMan>&, RemoteNodePtr&);

    bool gotPing(const TaskPtr<IPacMan>&, RemoteNodePtr&);
    bool gotSSIntroduceConsensusReply();

    void askForMissingPackages();
    void requestMissing(const cs::Hash&, const uint16_t, const uint64_t);

    /* Actions */
    bool good_;
    cs::LockFreeChanger<Config> config_;

    static const uint32_t maxPacksQueue_ = 2048;
    static const uint32_t maxRemoteNodes_ = 4096;

    cs::SpinLock sendPacksFlag_{ATOMIC_FLAG_INIT};

    struct PackSendTask {
        Packet pack;
        uint32_t resendTimes = 0;
        bool incrementId;
    };

    FixedCircularBuffer<PackSendTask, maxPacksQueue_> sendPacks_;

    TypedAllocator<RemoteNode> remoteNodes_;

    FixedHashMap<ip::udp::endpoint, RemoteNodePtr, uint16_t, maxRemoteNodes_> remoteNodesMap_;

    RegionAllocator netPacksAllocator_;
    cs::PublicKey myPublicKey_;

    cs::IPackStream iPackStream_;

    cs::SpinLock oLock_{ATOMIC_FLAG_INIT};
    cs::OPackStream oPackStream_;

    // SS Data
    SSBootstrapStatus ssStatus_ = SSBootstrapStatus::Empty;
    ip::udp::endpoint ssEp_;

    // Registration data
    Packet regPack_;
    uint64_t* regPackConnId_;
    bool acceptRegistrations_ = false;

    struct PostponedPacket {
        cs::RoundNumber round;
        MsgTypes type;
        Packet pack;

        PostponedPacket(const cs::RoundNumber r, const MsgTypes t, const Packet& p)
        : round(r)
        , type(t)
        , pack(p) {
        }
    };

    static constexpr uint32_t posponedBufferSize_ = 1024;
    using PPBuf = FixedCircularBuffer<PostponedPacket, posponedBufferSize_>;

    PPBuf postponedPacketsFirst_;
    PPBuf postponedPacketsSecond_;

    static constexpr uint32_t posponedPointerBufferSize_ = 2;
    PPBuf* postponed_[posponedPointerBufferSize_] = {&postponedPacketsFirst_, &postponedPacketsSecond_};

    cs::SpinLock uLock_{ATOMIC_FLAG_INIT};
    FixedCircularBuffer<MessagePtr, PacketCollector::MaxParallelCollections> uncollected_;

    cs::Sequence maxBlock_ = 0;
    cs::Sequence maxBlockCount_;

    Network* net_;
    Node* node_;

    Neighbourhood nh_;

    static constexpr uint32_t fragmentsFixedMapSize_ = 10000;
    FixedHashMap<cs::Hash, cs::RoundNumber, uint16_t, fragmentsFixedMapSize_> fragOnRound_;

    std::atomic_bool sendLarge_ = false;

    cs::SpinLock aLock_{ATOMIC_FLAG_INIT};
    std::map<cs::PublicKey, EndpointData> addresses_;

public:
    inline static size_t cntDirtyAllocs = 0;
    inline static size_t cntCorruptedFragments = 0;
    inline static size_t cntExtraLargeNotSent = 0;
};

#endif  // TRANSPORT_HPP
