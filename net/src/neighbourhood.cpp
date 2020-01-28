#include <neighbourhood.hpp>

#include <cscrypto/cscrypto.hpp>
#include <csnode/configholder.hpp>
#include <csnode/conveyer.hpp>
#include <csnode/datastream.hpp>
#include <csnode/node.hpp>
#include <transport.hpp>

Neighbourhood::Neighbourhood(Transport* transport, Node* node)
: transport_(transport)
, node_(node) {}

void Neighbourhood::processNeighbourMessage(const cs::PublicKey& sender, const Packet& pack) {
    switch (pack.getNetworkCommand()) {
        case NetworkCommand::VersionRequest:
            sendVersionReply(sender);
            break;

        case NetworkCommand::VersionReply:
            gotVersionReply(sender, pack);
            break;

        case NetworkCommand::Ping:
            sendPong(sender);
            break;

        case NetworkCommand::Pong:
            gotPong(sender, pack);
            break;

        default:
            cswarning() << "Unexpected network command";
            transport_->ban(sender);
    }
}

void Neighbourhood::newPeerDiscovered(const cs::PublicKey& peer) {
    sendVersionRequest(peer);
}

void Neighbourhood::peerDisconnected(const cs::PublicKey& peer) {
    removeFromCompatiblePool(peer);

    if (remove(peer)) {
        addFromCompatiblePool();
    }
}

void Neighbourhood::pingNeighbours() {
    std::lock_guard lock(neighbourMutex_);
    for (auto& n : neighbours_) {
        sendPing(n.first);
    }
}

void Neighbourhood::sendVersionRequest(const cs::PublicKey& receiver) {
    transport_->sendDirect(formPacket(BaseFlags::NetworkMsg,
                                      NetworkCommand::VersionRequest), receiver);
}

void Neighbourhood::sendVersionReply(const cs::PublicKey& receiver) {
    transport_->sendDirect(formPacket(BaseFlags::NetworkMsg,
                                      NetworkCommand::VersionReply,
                                      NODE_VERSION,
                                      node_->getBlockChain().uuid(),
                                      node_->getBlockChain().getLastSeq(),
                                      cs::Conveyer::instance().currentRoundNumber()),
                           receiver);
}

void Neighbourhood::sendPing(const cs::PublicKey& receiver) {
    transport_->sendDirect(formPacket(BaseFlags::NetworkMsg, NetworkCommand::Ping), receiver);
}

void Neighbourhood::sendPong(const cs::PublicKey& receiver) {
    transport_->sendDirect(formPacket(BaseFlags::NetworkMsg,
                                      NetworkCommand::Pong,
                                      node_->getBlockChain().getLastSeq(),
                                      cs::Conveyer::instance().currentRoundNumber()),
                           receiver);
}

void Neighbourhood::gotVersionReply(const cs::PublicKey& sender, const Packet& pack) {
    PeerInfo info;
    cs::IDataStream stream(pack.getMsgData(), pack.getMsgSize());
    stream >> info.nodeVersion;
    stream >> info.uuid;
    stream >> info.lastSeq;
    stream >> info.roundNumber;
    info.permanent = isPermanent(sender);

    tryToAddNew(sender, info);
}

void Neighbourhood::gotPong(const cs::PublicKey& sender, const Packet& pack) {
    cs::Sequence sequence = 0;
    bool result = false;

    {
        std::lock_guard lock(neighbourMutex_);
        auto neighbour = neighbours_.find(sender);

        if (neighbour != neighbours_.end()) {
            PeerInfo& info = neighbour->second;

            cs::IDataStream stream(pack.getMsgData(), pack.getMsgSize());
            stream >> info.lastSeq;
            stream >> info.roundNumber;

            sequence = info.lastSeq;
            result = true;
        }
    }

    if (result) {
        emit neighbourPingReceived(sequence, sender);
    }
}

bool Neighbourhood::isCompatible(const PeerInfo& info) const {
    if (info.nodeVersion < cs::ConfigHolder::instance().config()->getMinCompatibleVersion()) {
        return false;
    }

    auto myUuid = node_->getBlockChain().uuid();
    if (myUuid && info.uuid && myUuid != info.uuid) {
        return false;
    }

    return true;
}

void Neighbourhood::tryToAddNew(const cs::PublicKey& peer, const PeerInfo& info) {
    if (!isCompatible(info)) {
        return;
    }

    if (contains(peer)) {
        return;
    }

    if (isLimitReached() && !info.permanent) {
        addToCompatiblePool(peer);
        return;
    }

    removeFromCompatiblePool(peer);

    std::lock_guard lock(neighbourMutex_);
    neighbours_[peer] = info;

    transport_->onNeighboursChanged(peer, info.lastSeq, info.roundNumber, true);
}

bool Neighbourhood::remove(const cs::PublicKey& peer) {
    std::lock_guard lock(neighbourMutex_);
    auto it = neighbours_.find(peer);
    if (it != neighbours_.end()) {
        transport_->onNeighboursChanged(it->first, it->second.lastSeq, it->second.roundNumber, false);
        neighbours_.erase(it);
        return true;
    }
    return false;
}

void Neighbourhood::addToCompatiblePool(const cs::PublicKey& peer) {
    std::lock_guard lock(peersMux_);
    compatiblePeers_.insert(peer);
}

void Neighbourhood::removeFromCompatiblePool(const cs::PublicKey& peer) {
    std::lock_guard lock(peersMux_);
    compatiblePeers_.erase(peer);
}

void Neighbourhood::addFromCompatiblePool() {
    std::lock_guard lock(peersMux_);
    for (auto& n : compatiblePeers_) {
        sendVersionRequest(n);
    }
}

bool Neighbourhood::isLimitReached() const {
    std::lock_guard lock(neighbourMutex_);
    return neighbours_.size() >= kMaxNeighbours;
}

void Neighbourhood::setPermanentNeighbours(const std::set<cs::PublicKey>& permanent) {
    std::lock_guard lock(permNeighbourMux_);
    permanentNeighbours_ = decltype(permanentNeighbours_)(permanent.begin(), permanent.end());
}

bool Neighbourhood::isPermanent(const cs::PublicKey& peer) const {
    std::lock_guard lock(permNeighbourMux_);
    return permanentNeighbours_.find(peer) != permanentNeighbours_.end();
}

void Neighbourhood::forEachNeighbour(NeighboursCallback callback) {
    std::lock_guard<std::mutex> lock(neighbourMutex_);
    for (auto& n : neighbours_) {
        callback(n.first, n.second.lastSeq, n.second.roundNumber);
    }
}

uint32_t Neighbourhood::getNeighboursCount() const {
    std::lock_guard<std::mutex> lock(neighbourMutex_);
    return static_cast<uint32_t>(neighbours_.size());
}

bool Neighbourhood::contains(const cs::PublicKey& neighbour) const {
    std::lock_guard<std::mutex> lock(neighbourMutex_);
    return neighbours_.find(neighbour) != neighbours_.end();
}

template<class... Args>
Packet Neighbourhood::formPacket(BaseFlags flags, NetworkCommand cmd, Args&&... args) {
    cs::Bytes packetBytes;
    cs::ODataStream stream(packetBytes);
    stream << flags;
    stream << cmd;
    (void)(stream << ... << std::forward<Args>(args));
    return Packet(std::move(packetBytes));
}
