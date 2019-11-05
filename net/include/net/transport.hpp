/* Send blaming letters to @yrtimd */
#ifndef TRANSPORT_HPP
#define TRANSPORT_HPP

#include <atomic>
#include <csignal>
#include <list>
#include <mutex>

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

inline volatile std::sig_atomic_t gSignalStatus = 0;

using ConnectionId = uint64_t;
using Tick = uint64_t;

using PingSignal = cs::Signal<void(cs::Sequence, const cs::PublicKey&)>;

enum class NetworkCommand : uint8_t {
    Registration = 2,
    RegistrationConfirmed,
    RegistrationRefused,
    Ping,
    BlockSyncRequest
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

    void processNodeMessage(const Packet&);

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

    void OnMessageReceived(const net::NodeId&, net::ByteVector&&) override;

public signals:
    PingSignal pingReceived;
    cs::Action mainThreadIterated;

public slots:
    void onConfigChanged(const Config& updated);

private:
    void dispatchNodeMessage(const MsgTypes, const cs::RoundNumber, const Packet&, const uint8_t* data, size_t);

// Network packages processing - beg
// @TODO protocol
    void processNetworkMessage(const Packet&);

    Packet regPack_;
    void formRegPack(uint64_t uuid);
    void addMyOut(const uint8_t initFlagValue = 0); // to Reg Pack

    bool gotRegistrationRequest();
    bool gotRegistrationConfirmation();
    bool gotRegistrationRefusal();
    bool gotPing();

    void sendRegistrationRequest();
    void sendRegistrationConfirmation();
    void sendRegistrationRefusal(const RegistrationRefuseReasons reason);
    void sendPingPack();
// Network packages processing - end

    bool good_;
    cs::LockFreeChanger<Config> config_;
    RegionAllocator netPacksAllocator_;

    cs::PublicKey myPublicKey_;

    cs::IPackStream iPackStream_;

    cs::SpinLock oLock_{ATOMIC_FLAG_INIT};
    cs::OPackStream oPackStream_;

    // Postpone logic - beg
    // @TODO move to Node
    void postponePacket(const cs::RoundNumber, const MsgTypes, const Packet&);
    bool shouldSendPacket(const Packet& pack);

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
    // Postpone logic - end




//---------------------------------------
    cs::Sequence maxBlock_ = 0;
    cs::Sequence maxBlockCount_;

    Node* node_;

    // new logic
    net::NodeId id_;
    net::Host host_;

    std::condition_variable newPacketsReceived_;
    std::mutex inboxMux_;
    std::list<Packet> inboxQueue_;

    void processorRoutine();

    // @TODO move to PacketValidator
    bool isBlackListed(const net::NodeId&) { return false; }
    void addStrike(const net::NodeId&) {}
};
#endif  // TRANSPORT_HPP
