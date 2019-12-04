#include <neighbourhood.hpp>

#include <cscrypto/cscrypto.hpp>
#include <csnode/conveyer.hpp>
#include <csnode/datastream.hpp>
#include <csnode/node.hpp>
#include <transport.hpp>

Neighbourhood::Neighbourhood(Transport* transport, Node* node)
: transport_(transport)
, node_(node)
, uuid_(node_->getBlockChain().uuid()) {
}

void Neighbourhood::processNeighbourMessage(const cs::PublicKey& sender, const Packet& pack) {
    switch (pack.getNetworkCommand()) {
        case NetworkCommand::Registration:
            gotRegistrationRequest(sender, pack);
            break;

        case NetworkCommand::RegistrationConfirmed:
            gotRegistrationConfirmation(sender, pack);
            break;

        case NetworkCommand::RegistrationRefused:
            gotRegistrationRefusal(sender, pack);
            break;

        case NetworkCommand::Ping:
            gotPing(sender, pack);
            break;

        default:
            cswarning() << "Unexpected network command";
            transport_->ban(sender);
    }
}

void Neighbourhood::newPeerDiscovered(const cs::PublicKey& peer) {
    {
        std::lock_guard<std::mutex> lock(neighbourMutex_);
        if (neighbours_.size() >= kMaxNeighbours) {
            return;
        }

        if (neighbours_.find(peer) != neighbours_.end()) {
            return;
        }

        PeerInfo info;
        info.lastSeen = std::chrono::steady_clock::now();
        neighbours_[peer] = info;
    }

    sendRegistrationRequest(peer);
}

void Neighbourhood::peerDisconnected(const cs::PublicKey& peer) {
    std::lock_guard<std::mutex> lock(neighbourMutex_);
    if (neighbours_.find(peer) != neighbours_.end()) {
        neighbours_.erase(peer);
    }
}

void Neighbourhood::removeSilent() {
    using namespace std::chrono;
    auto now = steady_clock::now();

    std::lock_guard<std::mutex> lock(neighbourMutex_);
    for (auto it = neighbours_.begin(); it != neighbours_.end();) {
        if (duration_cast<seconds>(now - it->second.lastSeen) > kLastSeenTimeout) {
            sendRegistrationRefusal(it->first, RegistrationRefuseReasons::Timeout);
            if (it->second.connectionEstablished) {
                transport_->onNeighboursChanged(it->first, it->second.lastSeq, it->second.roundNumber, false);
            }
            it = neighbours_.erase(it);
        }
        else {
            ++it;
        }
    }
}

void Neighbourhood::pingNeighbours() {
    std::lock_guard<std::mutex> lock(neighbourMutex_);
    for (auto& n : neighbours_) {
        sendPingPack(n.first);
    }
}

void Neighbourhood::sendRegistrationRequest(const cs::PublicKey& receiver) {
    transport_->sendDirect(formPacket(BaseFlags::NetworkMsg,
                                      NetworkCommand::Registration,
                                      NODE_VERSION,
                                      uuid_,
                                      node_->getBlockChain().getLastSeq(),
                                      cs::Conveyer::instance().currentRoundNumber()), receiver);
}

void Neighbourhood::gotRegistrationRequest(const cs::PublicKey& sender, const Packet& pack) {
    std::lock_guard<std::mutex> lock(neighbourMutex_);
    if (neighbours_.size() >= kMaxNeighbours) {
        sendRegistrationRefusal(sender, RegistrationRefuseReasons::LimitReached);
        return;
    }

    auto it = neighbours_.find(sender);
    if (it != neighbours_.end() && it->second.connectionEstablished) {
        return;
    }

    cs::DataStream stream(pack.getMsgData(), pack.getMsgSize());

    PeerInfo info;
    stream >> info.nodeVersion;
    if (info.nodeVersion != NODE_VERSION) {
        sendRegistrationRefusal(sender, RegistrationRefuseReasons::BadClientVersion);
        return;
    }

    stream >> info.uuid;
    if (info.uuid != uuid_) {
        sendRegistrationRefusal(sender, RegistrationRefuseReasons::IncompatibleBlockchain);
        return;
    }

    stream >> info.lastSeq;
    stream >> info.roundNumber;
    info.lastSeen = std::chrono::steady_clock::now();
    info.connectionEstablished = true;

    sendRegistrationConfirmation(sender);
    transport_->onNeighboursChanged(sender, info.lastSeq, info.roundNumber, true);

    neighbours_[sender] = info;
}

void Neighbourhood::sendRegistrationConfirmation(const cs::PublicKey& receiver) {
    transport_->sendDirect(formPacket(BaseFlags::NetworkMsg,
                                      NetworkCommand::RegistrationConfirmed,
                                      node_->getBlockChain().getLastSeq(),
                                      cs::Conveyer::instance().currentRoundNumber()), receiver);
}

void Neighbourhood::gotRegistrationConfirmation(const cs::PublicKey& sender, const Packet& pack) {
    std::lock_guard<std::mutex> lock(neighbourMutex_);

    auto neighbour = neighbours_.find(sender); // got registration request or send it
    if (neighbour != neighbours_.end()) {
        PeerInfo& info = neighbour->second;
        auto now = std::chrono::steady_clock::now();

        // check timeout
        if (std::chrono::duration_cast<std::chrono::seconds>(now - info.lastSeen) > kLastSeenTimeout) {
            sendRegistrationRefusal(sender, RegistrationRefuseReasons::Timeout);
            if (info.connectionEstablished) {
                transport_->onNeighboursChanged(sender, info.lastSeq, info.roundNumber, false);
            }

            neighbours_.erase(neighbour);
            return;
        }

        info.lastSeen = now;
        if (info.connectionEstablished) {
            return;
        }

        cs::DataStream stream(pack.getMsgData(), pack.getMsgSize());
        stream >> info.lastSeq;
        stream >> info.roundNumber;
        info.connectionEstablished = true;
        transport_->onNeighboursChanged(sender, info.lastSeq, info.roundNumber, true);

        if (!info.nodeVersion) { // case we have no info about peer yet
            info.nodeVersion = NODE_VERSION;
            info.uuid = uuid_;
        }
    }
}

void Neighbourhood::sendRegistrationRefusal(const cs::PublicKey& receiver,
                                            const RegistrationRefuseReasons reason) {
   transport_->sendDirect(formPacket(BaseFlags::NetworkMsg,
                                     NetworkCommand::RegistrationRefused,
                                     static_cast<uint8_t>(reason)), receiver);
}

void Neighbourhood::gotRegistrationRefusal(const cs::PublicKey& sender, const Packet& pack) {
    RegistrationRefuseReasons reason;
    cs::DataStream stream(pack.getMsgData(), pack.getMsgSize());
    stream >> reason;
    cslog() << "Registration to " << EncodeBase58(sender.data(), sender.data() + sender.size())
            << " refused: " << parseRefusalReason(reason);

    std::lock_guard<std::mutex> lock(neighbourMutex_);
    auto it = neighbours_.find(sender);
    if (it != neighbours_.end()) {
        if (it->second.connectionEstablished) {
            transport_->onNeighboursChanged(sender, it->second.lastSeq, it->second.roundNumber, false);
        }
        neighbours_.erase(it);
    }
}

void Neighbourhood::sendPingPack(const cs::PublicKey& receiver) {
    transport_->sendDirect(formPacket(BaseFlags::NetworkMsg,
                                      NetworkCommand::Ping,
                                      node_->getBlockChain().getLastSeq(),
                                      cs::Conveyer::instance().currentRoundNumber()), receiver);
}

void Neighbourhood::gotPing(const cs::PublicKey& sender, const Packet& pack) {
    cs::Sequence sequence = 0;

    {
        std::lock_guard lock(neighbourMutex_);
        auto neighbour = neighbours_.find(sender);

        if (neighbour != neighbours_.end() && neighbour->second.nodeVersion) {
            auto now = std::chrono::steady_clock::now();
            PeerInfo& info = neighbour->second;

            if (std::chrono::duration_cast<std::chrono::seconds>(now - info.lastSeen) > kLastSeenTimeout) {
                sendRegistrationRefusal(sender, RegistrationRefuseReasons::Timeout);

                if (info.connectionEstablished) {
                    transport_->onNeighboursChanged(sender, info.lastSeq, info.roundNumber, false);
                }

                neighbours_.erase(neighbour);
                return;
            }

            info.lastSeen = now;

            cs::DataStream stream(pack.getMsgData(), pack.getMsgSize());
            stream >> info.lastSeq;
            stream >> info.roundNumber;

            sequence = info.lastSeq;
        }
    }

    if (sequence) {
        emit neighbourPingReceived(sequence, sender);
    }
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

std::string Neighbourhood::parseRefusalReason(RegistrationRefuseReasons reason) {
    std::string reasonInfo;

    switch (reason) {
        case RegistrationRefuseReasons::BadClientVersion:
            reasonInfo = "incompatible node version";
            break;
        case RegistrationRefuseReasons::IncompatibleBlockchain:
            reasonInfo = "incompatible blockchain version";
            break;
        case RegistrationRefuseReasons::LimitReached:
            reasonInfo = "maximum connections limit on remote node is reached";
            break;
        case RegistrationRefuseReasons::Timeout:
            reasonInfo = "timeout";
            break;
        default: {
            std::ostringstream os;
            os << "reason code " << static_cast<int>(reason);
            reasonInfo = os.str();
        }
    }

    return reasonInfo;
}

template<class... Args>
Packet Neighbourhood::formPacket(BaseFlags flags, NetworkCommand cmd, Args&&... args) {
    cs::Bytes packetBytes;
    cs::DataStream stream(packetBytes);
    stream << flags;
    stream << cmd;
    (void)(stream << ... << std::forward<Args>(args));
    return Packet(std::move(packetBytes));
}
