#include <neighbourhood.hpp>

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
        default: {
            std::ostringstream os;
            os << "reason code " << static_cast<int>(reason);
            reasonInfo = os.str();
        }
    }

    return reasonInfo;
}
} // namespace

void Neighbourhood::processNeighbourMessage(const cs::PublicKey& sender, const Packet& pack) {
    iPackStream_.init(pack.getMsgData(), pack.getMsgSize());

    NetworkCommand cmd;
    iPackStream_ >> cmd;

    if (!iPackStream_.good()) {
//        @TODO use validator
//        return sender->addStrike();
        return;
    }

    bool result = true;
    switch (cmd) {
        case NetworkCommand::Registration:
            result = gotRegistrationRequest();
            break;
        case NetworkCommand::RegistrationConfirmed:
            result = gotRegistrationConfirmation();
            break;
        case NetworkCommand::RegistrationRefused:
            result = gotRegistrationRefusal();
            break;
        case NetworkCommand::Ping:
            gotPing();
            break;
        default:
            result = false;
            cswarning() << "Unexpected network command";
    }

    if (!result) {
//        @TODO use validator
//        sender->addStrike();
    }
}

void Neighbourhood::formRegPack(uint64_t /* uuid */) {
//    oPackStream_.init(BaseFlags::NetworkMsg);
//    oPackStream_ << NetworkCommand::Registration << NODE_VERSION << uuid;
//    oPackStream_ << static_cast<ConnectionId>(0) << myPublicKey_;
}

void Neighbourhood::sendRegistrationRequest() {
    // send regPack_
}

bool Neighbourhood::gotRegistrationRequest() {
    // check from iPackStream_:
    // 1. NodeVersion version
    // 2. uint64 remoteUuid
    // 3. maybe connection id
    // 4. maybe blockchain top
    return false;
}

void Neighbourhood::sendRegistrationConfirmation() {
//    for example:
//    oPackStream_.init(BaseFlags::NetworkMsg);
//    oPackStream_ << NetworkCommand::RegistrationConfirmed << myPublicKey_;
//    sendDirect(oPackStream_.getPackets(), conn);
//    oPackStream_.clear();
}

bool Neighbourhood::gotRegistrationConfirmation() {
    cs::PublicKey key;
    iPackStream_ >> key;

    if (!iPackStream_.good()) {
        return false;
    }

    return true;
}

void Neighbourhood::sendRegistrationRefusal(const RegistrationRefuseReasons) {}

bool Neighbourhood::gotRegistrationRefusal() {
    RegistrationRefuseReasons reason;
    iPackStream_ >> reason;

    if (!iPackStream_.good() || !iPackStream_.end()) {
        return false;
    }

    std::string reasonInfo = parseRefusalReason(reason);
//    cslog() << "Registration to " << task->sender << " refused: " << reasonInfo;

    return true;
}

// Turn on testing blockchain ID in PING packets to prevent nodes from confuse alien ones
#define PING_WITH_BCHID

void Neighbourhood::sendPingPack() {
/*
    cs::Sequence seq = node_->getBlockChain().getLastSeq();
    cs::Lock lock(oLock_);
    oPackStream_.init(BaseFlags::NetworkMsg);
    Connection::Id id(0); // do we need connection id?
    oPackStream_ << NetworkCommand::Ping << id << seq << myPublicKey_;

#if defined(PING_WITH_BCHID)
    oPackStream_ << node_->getBlockChain().uuid();
#endif

    if (!config_->isCompatibleVersion()) {
        oPackStream_ << NODE_VERSION;
    }

    sendDirect(oPackStream_.getPackets(), conn);
    oPackStream_.clear();
*/
}

bool Neighbourhood::gotPing() {
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
