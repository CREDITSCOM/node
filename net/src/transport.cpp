/* Send blaming letters to @yrtimd */
#include "transport.hpp"

#include <algorithm>
#include <thread>

#include <csnode/node.hpp>
#include <csnode/conveyer.hpp>
#include <csnode/packstream.hpp>
#include <lib/system/allocators.hpp>
#include <lib/system/utils.hpp>

#include <packetvalidator.hpp>

namespace {
cs::PublicKey toPublicKey(const net::NodeId& id) {
    auto ptr = reinterpret_cast<const uint8_t*>(id.GetPtr());
    cs::PublicKey ret;
    std::copy(ptr, ptr + id.size(), ret.data());
    return ret;
}
} // namespace

// Signal transport to stop and stop Node
static void stopNode() noexcept(false) {
    Node::requestStop();
    // Transport::stop();
}

// Called periodically to poll the signal flag.
void pollSignalFlag() {
    if (gSignalStatus == 1) {
        gSignalStatus = 0;
        try {
            stopNode();
        }
        catch (...) {
            cserror() << "Poll signal error!";
            std::raise(SIGABRT);
        }
    }
}

constexpr cs::RoundNumber getRoundTimeout(const MsgTypes type) {
    switch (type) {
        case MsgTypes::FirstSmartStage:
        case MsgTypes::SecondSmartStage:
        case MsgTypes::ThirdSmartStage:
        case MsgTypes::RejectedContracts:
            return 100;
        case MsgTypes::TransactionPacket:
        case MsgTypes::TransactionsPacketRequest:
        case MsgTypes::TransactionsPacketReply:
            return cs::Conveyer::MetaCapacity;
        default:
            return 5;
    }
}

// Extern function dfined in main.cpp to poll and handle signal status.
extern void pollSignalFlag();

static std::string parseRefusalReason(RegistrationRefuseReasons reason) {
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
    default:
        {
            std::ostringstream os;
            os << "reason code " << static_cast<int>(reason);
            reasonInfo = os.str();
        }
        break;
    }

    return reasonInfo;
}

Transport::Transport(const Config& config, Node* node)
: config_(config)
, node_(node)
, myPublicKey_(node->getNodeIdKey())
, host_(net::Config(id_), static_cast<HostEventHandler&>(*this)) {
    good_ = true;
}

void Transport::run() {
  host_.Run();
  processorRoutine();
}

void Transport::OnMessageReceived(const net::NodeId& id, net::ByteVector&& data) {
    {
        std::lock_guard<std::mutex> g(inboxMux_);
        inboxQueue_.emplace_back(std::make_pair(toPublicKey(id), Packet(std::move(data))));
    }
    newPacketsReceived_.notify_one();
}

void Transport::OnNodeDiscovered(const net::NodeId& id) {
    std::lock_guard g(peersMux_);
    knownPeers_.insert(id);
}

void Transport::OnNodeRemoved(const net::NodeId& id) {
    std::lock_guard g(peersMux_);
    knownPeers_.erase(id);
}

void Transport::processorRoutine() {
    while (true) {
        std::unique_lock lk(inboxMux_);
        newPacketsReceived_.wait(lk, [this]() { return !inboxQueue_.empty(); });

        while (!inboxQueue_.empty()) {
            Packet pack(std::move(inboxQueue_.front().second));
            cs::PublicKey sender(inboxQueue_.front().first);
            inboxQueue_.pop_front();

            if (cs::PacketValidator::instance().validate(pack)) {
                if (pack.isNetwork()) {
                    neighbourhood_.processNeighbourMessage(sender, pack);
                }
                else {
                    processNodeMessage(sender, pack);
                }
            }
        }
    }
}

void Transport::processNetworkMessage(const cs::PublicKey& sender, const Packet& pack) {
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

void Transport::formRegPack(uint64_t /* uuid */) {
//    oPackStream_.init(BaseFlags::NetworkMsg);
//    oPackStream_ << NetworkCommand::Registration << NODE_VERSION << uuid;
//    oPackStream_ << static_cast<ConnectionId>(0) << myPublicKey_;
}

void Transport::sendRegistrationRequest() {
    // send regPack_
}

bool Transport::gotRegistrationRequest() {
    // check from iPackStream_:
    // 1. NodeVersion version
    // 2. uint64 remoteUuid
    // 3. maybe connection id
    // 4. maybe blockchain top
    return false;
}

void Transport::sendRegistrationConfirmation() {
//    for example:
//    oPackStream_.init(BaseFlags::NetworkMsg);
//    oPackStream_ << NetworkCommand::RegistrationConfirmed << myPublicKey_;
//    sendDirect(oPackStream_.getPackets(), conn);
//    oPackStream_.clear();
}

bool Transport::gotRegistrationConfirmation() {
    cs::PublicKey key;
    iPackStream_ >> key;

    if (!iPackStream_.good()) {
        return false;
    }

    return true;
}

void Transport::sendRegistrationRefusal(const RegistrationRefuseReasons) {}

bool Transport::gotRegistrationRefusal() {
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

void Transport::sendPingPack() {
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

bool Transport::gotPing() {
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

void Transport::processNodeMessage(const cs::PublicKey& sender, const Packet& pack) {
    auto type = pack.getType();
    auto rNum = pack.getRoundNum();

    switch (node_->chooseMessageAction(rNum, type, sender)) {
        case Node::MessageActions::Process:
            return dispatchNodeMessage(sender, type, rNum, pack.getMsgData(), pack.getMsgSize());
        case Node::MessageActions::Postpone:
            return postponePacket(rNum, type, pack);
        case Node::MessageActions::Drop:
            return;
    }
}

void Transport::dispatchNodeMessage(const cs::PublicKey& sender, const MsgTypes type, const cs::RoundNumber rNum, const uint8_t* data, size_t size) {
    if (size == 0) {
        cserror() << "Bad packet size, why is it zero?";
        return;
    }

    // never cut packets
    switch (type) {
        case MsgTypes::BlockRequest:
            return node_->getBlockRequest(data, size, sender);
        case MsgTypes::RequestedBlock:
            return node_->getBlockReply(data, size);
        case MsgTypes::BigBang:  // any round (in theory) may be set
            return node_->getBigBang(data, size, rNum);
        case MsgTypes::RoundTableRequest:  // old-round node may ask for round info
            return node_->getRoundTableRequest(data, size, rNum, sender);
        case MsgTypes::NodeStopRequest:
            return node_->getNodeStopRequest(rNum, data, size);
        case MsgTypes::RoundTable:
            return node_->getRoundTable(data, size, rNum, sender);
        case MsgTypes::RoundTableSS:
            return node_->getRoundTableSS(data, size, rNum);
        default:
            break;
    }

    // cut slow packs
    if ((rNum + getRoundTimeout(type)) < cs::Conveyer::instance().currentRoundNumber()) {
        csdebug() << "TRANSPORT> Ignore old packs, round " << rNum << ", type " << Packet::messageTypeToString(type);
        return;
    }

    if (type == MsgTypes::ThirdSmartStage) {
        csdebug() << "+++++++++++++++++++  ThirdSmartStage arrived +++++++++++++++++++++";
    }

    // packets which transport may cut
    switch (type) {
        case MsgTypes::BlockHash:
            return node_->getHash(data, size, rNum, sender);
        case MsgTypes::HashReply:
            return node_->getHashReply(data, size, rNum, sender);
        case MsgTypes::TransactionPacket:
            return node_->getTransactionsPacket(data, size);
        case MsgTypes::TransactionsPacketRequest:
            return node_->getPacketHashesRequest(data, size, rNum, sender);
        case MsgTypes::TransactionsPacketReply:
            return node_->getPacketHashesReply(data, size, rNum, sender);
        case MsgTypes::FirstStage:
            return node_->getStageOne(data, size, sender);
        case MsgTypes::SecondStage:
            return node_->getStageTwo(data, size, sender);
        case MsgTypes::FirstStageRequest:
            return node_->getStageRequest(type, data, size, sender);
        case MsgTypes::SecondStageRequest:
            return node_->getStageRequest(type, data, size, sender);
        case MsgTypes::ThirdStageRequest:
            return node_->getStageRequest(type, data, size, sender);
        case MsgTypes::ThirdStage:
            return node_->getStageThree(data, size);
        case MsgTypes::FirstSmartStage:
            return node_->getSmartStageOne(data, size, rNum, sender);
        case MsgTypes::SecondSmartStage:
            return node_->getSmartStageTwo(data, size, rNum, sender);
        case MsgTypes::ThirdSmartStage:
            return node_->getSmartStageThree(data, size, rNum, sender);
        case MsgTypes::SmartFirstStageRequest:
            return node_->getSmartStageRequest(type, data, size, sender);
        case MsgTypes::SmartSecondStageRequest:
            return node_->getSmartStageRequest(type, data, size, sender);
        case MsgTypes::SmartThirdStageRequest:
            return node_->getSmartStageRequest(type, data, size, sender);
        case MsgTypes::RejectedContracts:
            return node_->getSmartReject(data, size, rNum, sender);
        case MsgTypes::RoundTableReply:
            return node_->getRoundTableReply(data, size, sender);
        case MsgTypes::RoundPackRequest:
            return node_->getRoundPackRequest(data, size, rNum, sender);
        case MsgTypes::EmptyRoundPack:
            return node_->getEmptyRoundPack(data, size, rNum, sender);
        case MsgTypes::StateRequest:
            return node_->getStateRequest(data, size, rNum, sender);
        case MsgTypes::StateReply:
            return node_->getStateReply(data, size, rNum, sender);
        default:
            cserror() << "TRANSPORT> Unknown message type " << Packet::messageTypeToString(type) << " pack round " << rNum;
            break;
    }
}

bool Transport::shouldSendPacket(const Packet& pack) {
    if (pack.isNetwork()) {
        return false;
    }

    const cs::RoundNumber currentRound = cs::Conveyer::instance().currentRoundNumber();
    cs::RoundNumber rn = pack.getRoundNum() + getRoundTimeout(pack.getType());

    return !rn || rn >= currentRound;
}

inline void Transport::postponePacket(const cs::RoundNumber rNum, const MsgTypes type, const Packet& pack) {
    (*postponed_)->emplace(rNum, type, pack);
}

void Transport::processPostponed(const cs::RoundNumber /* rNum */) {
/*
    auto& ppBuf = *postponed_[1];
    for (auto& pp : **postponed_) {
        if (pp.round > rNum) {
            ppBuf.emplace(std::move(pp));
        }
        else if (pp.round == rNum) {
            dispatchNodeMessage(sender, pp.type, pp.round, pp.pack, pp.pack.getMsgData(), pp.pack.getMsgSize());
        }
    }

    (*postponed_)->clear();

    postponed_[1] = *postponed_;
    postponed_[0] = &ppBuf;

    csdebug() << "TRANSPORT> POSTPHONED finished, round " << rNum;
*/
}

uint32_t Transport::getNeighboursCount() {
    return 0;
}

uint32_t Transport::getMaxNeighbours() const {
    return config_->getMaxNeighbours();
}

void Transport::onConfigChanged(const Config& updated) {
    config_.exchange(updated);
}
