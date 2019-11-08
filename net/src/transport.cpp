/* Send blaming letters to @yrtimd */
#include "transport.hpp"

#include <algorithm>
#include <thread>

#include <cscrypto/cscrypto.hpp>
#include <csnode/node.hpp>
#include <csnode/conveyer.hpp>
#include <csnode/packstream.hpp>
#include <lib/system/structures.hpp>
#include <lib/system/utils.hpp>

#include <packetvalidator.hpp>

namespace {
cs::PublicKey toPublicKey(const net::NodeId& id) {
    auto ptr = reinterpret_cast<const uint8_t*>(id.GetPtr());
    cs::PublicKey ret;
    std::copy(ptr, ptr + id.size(), ret.data());
    return ret;
}

net::NodeId toNodeId(const cs::PublicKey& key) {
    const uint8_t* ptr = key.data();
    net::NodeId ret;
    std::copy(ptr, ptr + key.size(), reinterpret_cast<uint8_t*>(ret.GetPtr()));
    return ret;
}

net::Config createNetConfig(Config config, bool& good) {
    net::Config result(toNodeId(config.getMyPublicKey()));
    good = true;

    auto& ep = config.getInputEndpoint();
    result.listen_address = !ep.ip.empty() ? ep.ip : net::kLocalHost;
    result.listen_port = ep.port ? ep.port : net::kDefaultPort;
    result.traverse_nat = config.traverseNAT();

    auto& customBootNodes = config.getIpList();
    if (customBootNodes.empty()) {
        result.use_default_boot_nodes = true;
    }
    else {
        result.use_default_boot_nodes = false;
        for (auto& node : customBootNodes) {
            if (node.ip.empty() || node.id.empty() || node.port == 0) {
                good = false;
                break;
            }

            net::NodeEntrance entry;
            entry.address = net::bi::address::from_string(node.ip); // @TODO change it
            entry.udp_port = entry.tcp_port = node.port;
            std::vector<uint8_t> idBytes;
            if (!DecodeBase58(node.id, idBytes)) {
                good = false;
                break;
            }
            std::copy(idBytes.begin(), idBytes.end(), reinterpret_cast<uint8_t*>(entry.id.GetPtr()));
            result.custom_boot_nodes.push_back(entry);
        }
    }
    return result;
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

Transport::Transport(const Config& config, Node* node)
: config_(createNetConfig(config, good_))
, node_(node)
, host_(config_, static_cast<HostEventHandler&>(*this))
, neighbourhood_(this, node_) {}

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
    {
        std::lock_guard g(peersMux_);
        knownPeers_.insert(id);
    }
    neighbourhood_.newPeerDiscovered(toPublicKey(id));
}

void Transport::OnNodeRemoved(const net::NodeId& id) {
    {
        std::lock_guard g(peersMux_);
        knownPeers_.erase(id);
    }
    neighbourhood_.peerDisconnected(toPublicKey(id));
}

void Transport::sendDirect(Packet&& pack, const cs::PublicKey& receiver) {
    host_.SendDirect(toNodeId(receiver), pack.moveData());
}

void Transport::sendMulticast(Packet&& pack, const std::vector<cs::PublicKey>& receivers) {
    for (auto& receiver : receivers) {
        auto ptr = reinterpret_cast<const uint8_t*>(pack.data());
        host_.SendDirect(toNodeId(receiver), cs::Bytes(ptr, ptr + pack.size()));
    }
}

void Transport::sendBroadcast(Packet&& pack) {
    host_.SendBroadcast(pack.moveData());
}

void Transport::processorRoutine() {
    while (true) {
        CallsQueue::instance().callAll();
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
//    return config_->getMaxNeighbours();
    return Neighbourhood::MaxNeighbours;
}

void Transport::onConfigChanged(const Config& /* updated */) {
//    config_.exchange(updated);
}
