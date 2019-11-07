/* Send blaming letters to @yrtimd */
#ifndef TRANSPORT_HPP
#define TRANSPORT_HPP

#include <atomic>
#include <csignal>
#include <list>
#include <mutex>
#include <unordered_set>

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

class Node;

class Transport : public net::HostEventHandler {
public:
    inline static volatile std::sig_atomic_t gSignalStatus = 0;
    static void stop() { Transport::gSignalStatus = 1; }

    explicit Transport(const Config& config, Node* node);
    ~Transport() {}

    void run();
    bool isGood() const { return good_; }

    void processNodeMessage(const cs::PublicKey&, const Packet&);
    void processPostponed(const cs::RoundNumber); // @TODO move to Node

    void sendDirect(Packet&&, const cs::PublicKey&) {}
    void sendMulticast(Packet&&, const std::vector<cs::PublicKey>&) {}
    void sendBroadcast(Packet&&) {}

    // neighbours interface
    uint32_t getNeighboursCount();
    uint32_t getMaxNeighbours() const;
    void forEachNeighbour(std::function<bool(const cs::PublicKey&)>) {}
    bool hasNeighbour(const cs::PublicKey&) { return false; }
    cs::Sequence getNeighbourLastSequence(const cs::PublicKey&) { return 0; }

    // @TODO remove, used in Node
    void sendSSIntroduceConsensus(const std::vector<cs::PublicKey>&) {}

    // HostEventHandler
    void OnMessageReceived(const net::NodeId&, net::ByteVector&&) override;
    void OnNodeDiscovered(const net::NodeId&) override;
    void OnNodeRemoved(const net::NodeId&) override;

public signals:
    PingSignal pingReceived;
    cs::Action mainThreadIterated;

public slots:
    void onConfigChanged(const Config& updated);

private:
    void dispatchNodeMessage(const cs::PublicKey& sender, const MsgTypes,
                             const cs::RoundNumber, const uint8_t* data, size_t);

// Network packages processing - beg
// @TODO protocol
    void processNetworkMessage(const cs::PublicKey& sender, const Packet&);

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

    bool good_;
    cs::LockFreeChanger<Config> config_;

    cs::IPackStream iPackStream_;

    cs::Sequence maxBlock_ = 0;
    cs::Sequence maxBlockCount_;

    Node* node_;
    cs::PublicKey myPublicKey_;

    // new logic
    net::NodeId id_;
    net::Host host_;

    std::condition_variable newPacketsReceived_;
    std::mutex inboxMux_;
    std::list<std::pair<cs::PublicKey, Packet>> inboxQueue_;

    std::mutex peersMux_;
    std::unordered_set<net::NodeId> knownPeers_;

    Neighbourhood neighbourhood_;

    void processorRoutine();

    // @TODO move to PacketValidator
    bool isBlackListed(const net::NodeId&) { return false; }
    void addStrike(const net::NodeId&) {}
};
#endif  // TRANSPORT_HPP
