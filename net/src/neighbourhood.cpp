#include <neighbourhood.hpp>

#include <cscrypto/cscrypto.hpp>
#include <csnode/datastream.hpp>
#include <csnode/node.hpp>
#include <transport.hpp>

namespace {
std::string parseRefusalReason(RegistrationRefuseReasons reason) {
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
Packet formPacket(BaseFlags flags, NetworkCommand cmd, Args&&... args) {
    cs::Bytes packetBytes;
    cs::DataStream stream(packetBytes);
    stream << flags;
    stream << cmd;
    (void)(stream << ... << std::forward<Args>(args));
    return Packet(std::move(packetBytes));
}
} // namespace

Neighbourhood::Neighbourhood(Transport* transport, Node* node)
    : transport_(transport), node_(node), uuid_(node_->getBlockChain().uuid()) {}

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
    if (neighboursCount_ < MaxNeighbours) {
        sendRegistrationRequest(peer);    
    }
}

void Neighbourhood::peerDisconnected(const cs::PublicKey& peer) {
    std::lock_guard<std::mutex> g(neighbourMux_);
    if (neighbours_.find(peer) != neighbours_.end()) {
        neighbours_.erase(peer);
        --neighboursCount_;
    }
}

void Neighbourhood::sendRegistrationRequest(const cs::PublicKey& receiver) {
    transport_->sendDirect(formPacket(BaseFlags::NetworkMsg,
                                      NetworkCommand::Registration,
                                      NODE_VERSION,
                                      uuid_,
                                      node_->getBlockChain().getLastSeq()), receiver);

    PeerInfo info;
    info.lastSeen = std::chrono::steady_clock::now();

    std::lock_guard<std::mutex> g(neighbourMux_);
    neighbours_[receiver] = info;
    ++neighboursCount_;
}

void Neighbourhood::gotRegistrationRequest(const cs::PublicKey& sender, const Packet& pack) {
    if (neighboursCount_ >= MaxNeighbours) {
        sendRegistrationRefusal(sender, RegistrationRefuseReasons::LimitReached);
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
    info.lastSeen = std::chrono::steady_clock::now();
    sendRegistrationConfirmation(sender);

    std::lock_guard<std::mutex> g(neighbourMux_);
    neighbours_[sender] = info;
    ++neighboursCount_;
}

void Neighbourhood::sendRegistrationConfirmation(const cs::PublicKey& receiver) {
    transport_->sendDirect(formPacket(BaseFlags::NetworkMsg,
                                      NetworkCommand::RegistrationConfirmed,
                                      node_->getBlockChain().getLastSeq()), receiver);
}

void Neighbourhood::gotRegistrationConfirmation(const cs::PublicKey& sender, const Packet& pack) {
    std::lock_guard<std::mutex> g(neighbourMux_);

    auto neighbour = neighbours_.find(sender); // got registration request or send it
    if (neighbour != neighbours_.end()) {
        PeerInfo& info = neighbour->second;
        auto now = std::chrono::steady_clock::now();

        // check timeout
        if (std::chrono::duration_cast<std::chrono::seconds>(now - info.lastSeen) > LastSeenTimeout) {
            sendRegistrationRefusal(sender, RegistrationRefuseReasons::Timeout);
            neighbours_.erase(neighbour);
            --neighboursCount_;
            return;
        }

        info.lastSeen = now;
        cs::DataStream stream(pack.getMsgData(), pack.getMsgSize());
        stream >> info.lastSeq;
        info.connectionEstablished = true;

        if (!info.nodeVersion) { // case we have not info about peer yet
            info.nodeVersion = NODE_VERSION;
            info.uuid = uuid_;
            sendRegistrationConfirmation(sender); // let him measure timeout
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
}

void Neighbourhood::sendPingPack(const cs::PublicKey&) {
/*
    cs::Sequence seq = node_->getBlockChain().getLastSeq();
    cs::Lock lock(oLock_);
    oPackStream_.init(BaseFlags::NetworkMsg);
    Connection::Id id(0); // do we need connection id?
    oPackStream_ << NetworkCommand::Ping << id << seq << myPublicKey_;

    oPackStream_ << node_->getBlockChain().uuid();

    if (!config_->isCompatibleVersion()) {
        oPackStream_ << NODE_VERSION;
    }

    sendDirect(oPackStream_.getPackets(), conn);
    oPackStream_.clear();
*/
}

void Neighbourhood::gotPing(const cs::PublicKey&, const Packet&) {
/*    Connection::Id id = 0u;
    cs::Sequence lastSeq = 0u;

    cs::PublicKey publicKey;
    iPackStream_ >> id >> lastSeq >> publicKey;

#if defined(PING_WITH_BCHID)
    uint64_t remoteUuid = 0;
    iPackStream_ >> remoteUuid;

    auto uuid = node_->getBlockChain().uuid();

    if (uuid != 0 && remoteUuid != 0) {
        if (uuid != remoteUuid) {
            return false;   // remote is incompatible
        }
    }
#endif
    if (!config_->isCompatibleVersion() && iPackStream_.end()) {
//        nh_.gotBadPing(id);
        return false;
    }

    uint16_t nodeVersion = 0;

    if (!iPackStream_.end()) {
        iPackStream_ >> nodeVersion;
    }

    if (!iPackStream_.good() || !iPackStream_.end()) {
        return false;
    }

    if (lastSeq > maxBlock_) {
        maxBlock_ = lastSeq;
        maxBlockCount_ = 1;
    }

//    if (nh_.validateConnectionId(sender, id, task->sender, publicKey, lastSeq)) {
        emit pingReceived(lastSeq, publicKey);
//    }

    return true;
*/
}
