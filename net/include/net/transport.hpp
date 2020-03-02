/* Send blaming letters to @yrtimd */
#ifndef TRANSPORT_HPP
#define TRANSPORT_HPP

#include <atomic>
#include <csignal>
#include <list>
#include <map>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <vector>
#include <optional>

#include <config.hpp>

#include <lib/system/common.hpp>
#include <lib/system/logger.hpp>
#include <lib/system/signals.hpp>

#include <p2p_network.h>

#include "neighbourhood.hpp"
#include "packet.hpp"
#include "packetsqueue.hpp"

inline volatile std::sig_atomic_t gSignalStatus = 0;

using PingSignal = cs::Signal<void(cs::Sequence, const cs::PublicKey&)>;
using NeighbourAddedSignal = cs::Signal<void(const cs::PublicKey&, cs::Sequence, cs::RoundNumber)>;
using NeighbourRemovedSignal = cs::Signal<void(const cs::PublicKey&)>;

class Node;

class Transport : public net::HostEventHandler {
public:
    inline static volatile std::sig_atomic_t gSignalStatus = 0;
    static void stop() { Transport::gSignalStatus = 1; }

    using AddressAndPort = std::pair<std::string, uint16_t>;
    using BanList = std::vector<AddressAndPort>;

    explicit Transport(Node* node);
    ~Transport();

    void run();
    bool isGood() const { return good_; }

    void processNodeMessage(const cs::PublicKey&, const Packet&);
    void processPostponed(const cs::RoundNumber); // @TODO move to Node

    void sendDirect(Packet&&, const cs::PublicKey&);
    void sendMulticast(Packet&&, const std::vector<cs::PublicKey>&);
    void sendBroadcast(Packet&&);
    void sendBroadcastIfNoConnection(Packet&&, const cs::PublicKey&);

    void ban(const cs::PublicKey&);
    void revertBan(const cs::PublicKey&);
    void clearBanList();
    void getBanList(BanList&) const;

    // neighbours interface
    void setPermanentNeighbours(const std::set<cs::PublicKey>&);
    uint32_t getNeighboursCount() const;
    uint32_t getMaxNeighbours() const;
    void forEachNeighbour(Neighbourhood::NeighboursCallback);
    bool hasNeighbour(const cs::PublicKey&) const;
    void addToNeighbours(const std::set<cs::PublicKey>&);

    void getKnownPeers(std::vector<cs::PeerData>&);

    // from neigbours
    // @param added - true if new neighbour adder, false if removed
    void onNeighboursChanged(const cs::PublicKey&, cs::Sequence lastSeq,
                            cs::RoundNumber lastRound, bool added);
public slots:
    void onPingReceived(cs::Sequence sequence, const cs::PublicKey& key);

public signals:
    PingSignal pingReceived;
    cs::Action mainThreadIterated;
    NeighbourAddedSignal neighbourAdded;
    NeighbourRemovedSignal neighbourRemoved;

protected:
    // HostEventHandler
    void OnMessageReceived(const net::NodeId&, net::ByteVector&&) override;
    void OnNodeDiscovered(const net::NodeId&) override;
    void OnNodeRemoved(const net::NodeId&) override;

private:
// Postpone logic - beg
// @TODO move to Node
    void postponePacket(const cs::PublicKey& sender, const cs::RoundNumber, const Packet&);

    struct PostponedPack {
        cs::PublicKey sender;
        Packet pack;
    };
    std::map<cs::RoundNumber, std::vector<PostponedPack>> postponed_;
// Postpone logic - end

    void dispatchNodeMessage(const cs::PublicKey& sender, const MsgTypes,
                             const cs::RoundNumber, const uint8_t* data, size_t);
    void processorRoutine();
    void process();
    void checkNeighboursChange();

    bool good_ = false;
    net::Config config_;

    Node* node_;

    std::condition_variable newPacketsReceived_;
    std::mutex inboxMux_;
    PacketsQueue inboxQueue_;

    Neighbourhood neighbourhood_;
    std::thread processorThread_;

    struct NeighbourData {
        const cs::PublicKey key;
        cs::Sequence lastSeq;
        cs::RoundNumber lastRound;
        bool added; // true if should be added, false if should be removed

        NeighbourData(const cs::PublicKey& key, cs::Sequence s, cs::RoundNumber r, bool a)
            : key(key), lastSeq(s), lastRound(r), added(a) {}
    };

    std::mutex neighboursMux_;
    std::list<NeighbourData> neighboursToHandle_;

    net::Host host_;
};
#endif  // TRANSPORT_HPP
