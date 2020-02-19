#ifndef NEIGHBOURHOOD_HPP
#define NEIGHBOURHOOD_HPP

#include <functional>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <set>

#include <networkcommands.hpp>
#include <packet.hpp>

#include <lib/system/signals.hpp>

class Node;
class Transport;

class Neighbourhood {
public:
    using NeighboursCallback = std::function<void(const cs::PublicKey&, cs::Sequence, cs::RoundNumber)>;
    using NeighbourPingSignal = cs::Signal<void(cs::Sequence, const cs::PublicKey&)>;

    constexpr static uint32_t kMaxNeighbours = 16;
    constexpr static uint32_t kMinNeighbours = 1;

    constexpr static std::chrono::seconds kPingInterval{2};

    Neighbourhood(Transport*, Node*);

    void setPermanentNeighbours(const std::set<cs::PublicKey>&);
    
    void processNeighbourMessage(const cs::PublicKey& sender, const Packet&);
    void newPeerDiscovered(const cs::PublicKey&);
    void peerDisconnected(const cs::PublicKey&);

    void pingNeighbours();

    void forEachNeighbour(NeighboursCallback);
    uint32_t getNeighboursCount() const;
    bool contains(const cs::PublicKey& neighbour) const;
    void add(const std::set<cs::PublicKey>&);

public signals:
    NeighbourPingSignal neighbourPingReceived;

private:
    template<class... Args>
    static Packet formPacket(BaseFlags flags, NetworkCommand cmd, Args&&... args);

    struct PeerInfo {
        cs::Version nodeVersion = 0;
        uint64_t uuid = 0;
        cs::Sequence lastSeq = 0;
        cs::RoundNumber roundNumber = 0;
        bool permanent = false;
    };

    void sendVersionRequest(const cs::PublicKey& receiver);
    void sendVersionReply(const cs::PublicKey& receiver);
    void sendPing(const cs::PublicKey& receiver);
    void sendPong(const cs::PublicKey& receiver);

    void gotVersionReply(const cs::PublicKey&, const Packet&);
    void gotPong(const cs::PublicKey&, const Packet&);

    bool isLimitReached() const;
    bool isCompatible(const PeerInfo&) const;
    bool isPermanent(const cs::PublicKey&) const;

    void addToCompatiblePool(const cs::PublicKey&);
    void removeFromCompatiblePool(const cs::PublicKey&);
    void addFromCompatiblePool();

    void tryToAddNew(const cs::PublicKey&, const PeerInfo&);
    bool remove(const cs::PublicKey&);

    Transport* transport_;
    Node* node_;

    mutable std::mutex neighbourMutex_;
    std::unordered_map<cs::PublicKey, PeerInfo> neighbours_;

    mutable std::mutex peersMux_;
    std::unordered_set<cs::PublicKey> compatiblePeers_;

    mutable std::mutex permNeighbourMux_;
    std::unordered_set<cs::PublicKey> permanentNeighbours_;
};
#endif  // NEIGHBOURHOOD_HPP
