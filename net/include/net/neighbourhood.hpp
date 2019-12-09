#ifndef NEIGHBOURHOOD_HPP
#define NEIGHBOURHOOD_HPP

#include <chrono>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <optional>

#include <lib/system/signals.hpp>
#include <networkcommands.hpp>
#include <packet.hpp>

#include <lib/system/common.hpp>

class Node;
class Transport;

class Neighbourhood {
public:
    using NeighboursCallback = std::function<void(const cs::PublicKey&, cs::Sequence, cs::RoundNumber)>;
    using NeighbourPingSignal = cs::Signal<void(cs::Sequence, const cs::PublicKey&)>;

    constexpr static uint32_t kMaxNeighbours = 16;
    constexpr static uint32_t kMinNeighbours = 2;

    constexpr static std::chrono::seconds kPingInterval{2};

    Neighbourhood(Transport*, Node*);
    
    void processNeighbourMessage(const cs::PublicKey& sender, const Packet&);
    void newPeerDiscovered(const cs::PublicKey&);
    void peerDisconnected(const cs::PublicKey&);

    void removeSilent();
    void pingNeighbours();

    void forEachNeighbour(NeighboursCallback);
    uint32_t getNeighboursCount() const;
    bool contains(const cs::PublicKey& neighbour) const;

public signals:
    NeighbourPingSignal neighbourPingReceived;

private:
    static std::string parseRefusalReason(RegistrationRefuseReasons reason);

    template<class... Args>
    static Packet formPacket(BaseFlags flags, NetworkCommand cmd, Args&&... args);

    struct PeerInfo {
        cs::Version nodeVersion = 0;
        uint64_t uuid = 0;
        cs::Sequence lastSeq = 0;
        cs::RoundNumber roundNumber = 0;
        bool connectionEstablished = false;
        std::chrono::time_point<std::chrono::steady_clock> lastSeen;
    };

    void sendRegistrationRequest(const cs::PublicKey& receiver);
    void sendRegistrationConfirmation(const cs::PublicKey& receiver);
    void sendRegistrationRefusal(const cs::PublicKey& reciever, const RegistrationRefuseReasons reason);
    void sendPingPack(const cs::PublicKey& receiver);

    void gotRegistrationRequest(const cs::PublicKey& sender, const Packet&);
    void gotRegistrationConfirmation(const cs::PublicKey& sender, const Packet&);
    void gotRegistrationRefusal(const cs::PublicKey& sender, const Packet&);
    void gotPing(const cs::PublicKey&, const Packet&);

    Transport* transport_;
    Node* node_;

    mutable std::mutex neighbourMutex_;
    std::map<cs::PublicKey, PeerInfo> neighbours_;

    const uint64_t uuid_;
};
#endif  // NEIGHBOURHOOD_HPP
