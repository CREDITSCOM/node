/* Send blaming letters to @yrtimd */
#ifndef TRANSPORT_HPP
#define TRANSPORT_HPP

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

#include <p2p_network.h>

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

class Transport : public net::HostEventHandler {
public:
    explicit Transport(const Config& config, Node* node);
    ~Transport() {}

    void run();

    inline static volatile std::sig_atomic_t gSignalStatus = 0;

    static void stop() {
        Transport::gSignalStatus = 1;
    }

    static const char* networkCommandToString(NetworkCommand command);

    void processNetworkTask(const TaskPtr<IPacMan>&, RemoteNodePtr&);
    void processNodeMessage(const Packet&);

    const cs::PublicKey& getMyPublicKey() const {
        return myPublicKey_;
    }

    bool isGood() const {
        return good_;
    }

    void deliverDirect(const Packet*, const uint32_t, const cs::PublicKey&);
    void deliverBroadcast(const Packet*, const uint32_t);
    void deliverConfidants(const Packet* pack, const uint32_t size, const std::vector<cs::PublicKey>&, int except = -1);
    bool checkConfidants(const std::vector<cs::PublicKey>& list, int except = -1);

    void processPostponed(const cs::RoundNumber);

    // neighbours interface
    uint32_t getNeighboursCount();
    uint32_t getMaxNeighbours() const;
    ConnectionPtr getConnectionByNumber(const std::size_t number);
    cs::Sequence getConnectionLastSequence(const std::size_t number);

    void forEachNeighbour(std::function<bool(const cs::PublicKey&)>) {}
    bool hasNeighbour(const cs::PublicKey&) { return false; }
    cs::Sequence getNeighbourLastSequence(const cs::PublicKey&) { return 1; }

    void sendSSIntroduceConsensus(const std::vector<cs::PublicKey>&) {}
    void OnMessageReceived(const net::NodeId&, net::ByteVector&&) override {}

public signals:
    PingSignal pingReceived;
    cs::Action mainThreadIterated;

public slots:
    void onConfigChanged(const Config& updated);

private:
    bool shouldSendPacket(const Packet& pack);
    void sendRegistrationRequest(Connection& conn);
    void sendRegistrationConfirmation(const Connection& conn, const Connection::Id requestedId);
    void sendRegistrationRefusal(const Connection& conn, const RegistrationRefuseReasons reason);
    void sendPingPack(const Connection& conn);
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

    bool gotPackInform(const TaskPtr<IPacMan>&, RemoteNodePtr&);
    bool gotPackRenounce(const TaskPtr<IPacMan>&, RemoteNodePtr&);
    bool gotPackRequest(const TaskPtr<IPacMan>&, RemoteNodePtr&);

    bool gotPing(const TaskPtr<IPacMan>&, RemoteNodePtr&);
    bool gotSSIntroduceConsensusReply();

    /* Actions */
    bool good_;
    cs::LockFreeChanger<Config> config_;

    RegionAllocator netPacksAllocator_;
    cs::PublicKey myPublicKey_;

    cs::IPackStream iPackStream_;

    cs::SpinLock oLock_{ATOMIC_FLAG_INIT};
    cs::OPackStream oPackStream_;

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

    cs::Sequence maxBlock_ = 0;
    cs::Sequence maxBlockCount_;
    std::deque<ConnectionPtr> neighbours_;

    Node* node_;
    net::NodeId id_;
    net::Host host_;
};
#endif  // TRANSPORT_HPP
