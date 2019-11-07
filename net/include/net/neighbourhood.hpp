/* Send blaming letters to @yrtimd */
#ifndef NEIGHBOURHOOD_HPP
#define NEIGHBOURHOOD_HPP

#include <chrono>
#include <map>
#include <mutex>
#include <set>

#include <lib/system/common.hpp>
#include <networkcommands.hpp>
#include <packet.hpp>

class Node;
class Transport;

class Neighbourhood {
public:
    const static uint32_t MaxNeighbours = 16;
    const static uint32_t MinNeighbours = 2;

    Neighbourhood(Transport*, Node*);
    
    void processNeighbourMessage(const cs::PublicKey& sender, const Packet&);
    void newPeerDiscovered(const cs::PublicKey&);
    void peerDisconnected(const cs::PublicKey&);

private:
    constexpr static std::chrono::seconds LastSeenTimeout{5};

    struct PeerInfo {
        cs::Version nodeVersion = 0;
        uint64_t uuid = 0;
        cs::Sequence lastSeq = 0;
        bool connectionEstablished = false;
        std::chrono::time_point<std::chrono::steady_clock> lastSeen;
    };

    Packet formRegPack();

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

    std::mutex neighbourMux_;
    std::map<cs::PublicKey, PeerInfo> neighbours_;
    std::atomic<size_t> neighboursCount_ = 0;

    uint64_t uuid_;
};
#endif  // NEIGHBOURHOOD_HPP
