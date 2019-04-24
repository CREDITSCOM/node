/* Send blaming letters to @yrtimd */
#ifndef TRANSPORT_HPP
#define TRANSPORT_HPP

#include <boost/asio.hpp>
#include <csignal>

#include <client/config.hpp>

#include <csnode/node.hpp>
#include <csnode/packstream.hpp>

#include <lib/system/allocators.hpp>
#include <lib/system/cache.hpp>
#include <lib/system/common.hpp>
#include <lib/system/logger.hpp>
#include <lib/system/signals.hpp>

#include <net/network.hpp>

#include "neighbourhood.hpp"
#include "packet.hpp"
#include "pacmans.hpp"

inline volatile std::sig_atomic_t gSignalStatus = 0;

using ConnectionId = uint64_t;
using Tick = uint64_t;
using PingSignal = cs::Signal<void(cs::Sequence)>;

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
};

enum class RegistrationRefuseReasons : uint8_t {
    Unspecified,
    LimitReached,
    BadId,
    BadClientVersion,
    Timeout,
    BadResponse
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

class Transport {
public:
    Transport(const Config& config, Node* node)
    : config_(config)
    , sendPacksFlag_()
    , remoteNodes_(maxRemoteNodes_ + 1)
    , netPacksAllocator_(1 << 24, 1)
    , myPublicKey_(node->getNodeIdKey())
    , oLock_()
    , oPackStream_(&netPacksAllocator_, node->getNodeIdKey())
    , uLock_()
    , net_(new Network(config, this))
    , node_(node)
    , nh_(this) {
        good_ = net_->isGood();
    }

    ~Transport();

    void run();

    inline static volatile std::sig_atomic_t gSignalStatus = 0;

    static void stop() {
        Transport::gSignalStatus = 1;
    }

    RemoteNodePtr getPackSenderEntry(const ip::udp::endpoint&);

    void processNetworkTask(const TaskPtr<IPacMan>&, RemoteNodePtr&);
    void processNodeMessage(const Message&);
    void processNodeMessage(const Packet&);

    void addTask(Packet*, const uint32_t packNum, bool incrementWhenResend = false);
    void clearTasks();

    const cs::PublicKey& getMyPublicKey() const {
        return myPublicKey_;
    }

    bool isGood() const {
        return good_;
    }

    void sendBroadcast(const Packet* pack) {
        nh_.sendByNeighbours(pack);
    }

    bool sendDirect(const Packet*, const Connection&);
    void deliverDirect(const Packet*, const uint32_t, ConnectionPtr);
    void deliverBroadcast(const Packet*, const uint32_t);

    void gotPacket(const Packet&, RemoteNodePtr&);
    void redirectPacket(const Packet&, RemoteNodePtr&);
    bool shouldSendPacket(const Packet&);

    void refillNeighbourhood();
    void processPostponed(const cs::RoundNumber);

    void sendRegistrationRequest(Connection&);
    void sendRegistrationConfirmation(const Connection&, const Connection::Id);
    void sendRegistrationRefusal(const Connection&, const RegistrationRefuseReasons);
    void sendPackRenounce(const cs::Hash&, const Connection&);
    void sendPackInform(const Packet&, const Connection&);

    void sendPingPack(const Connection&);

    void registerMessage(MessagePtr);

    // neighbours interface
    uint32_t getNeighboursCount();
    uint32_t getNeighboursCountWithoutSS();
    uint32_t getMaxNeighbours() const;
    ConnectionPtr getSyncRequestee(const cs::Sequence seq, bool& alreadyRequested);
    ConnectionPtr getConnectionByKey(const cs::PublicKey& pk);
    ConnectionPtr getConnectionByNumber(const std::size_t number);
    ConnectionPtr getRandomNeighbour();

    std::unique_lock<cs::SpinLock> getNeighboursLock() const;

    // thread safe negihbours methods
    void forEachNeighbour(std::function<void(ConnectionPtr)> func);
    void forEachNeighbourWithoudSS(std::function<void(ConnectionPtr)> func);

    // no thread safe
    const Connections getNeighbours() const;
    const Connections getNeighboursWithoutSS() const;

    void syncReplied(const cs::Sequence seq);
    bool isPingDone();
    void resetNeighbours();

public signals:
    PingSignal pingReceived;

private:
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

    bool gotPackInform(const TaskPtr<IPacMan>&, RemoteNodePtr&);
    bool gotPackRenounce(const TaskPtr<IPacMan>&, RemoteNodePtr&);
    bool gotPackRequest(const TaskPtr<IPacMan>&, RemoteNodePtr&);

    bool gotPing(const TaskPtr<IPacMan>&, RemoteNodePtr&);

    void askForMissingPackages();
    void requestMissing(const cs::Hash&, const uint16_t, const uint64_t);

    /* Actions */
    bool good_;
    Config config_;

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
};

#endif  // TRANSPORT_HPP
