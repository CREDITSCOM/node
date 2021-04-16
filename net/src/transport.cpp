/* Send blaming letters to @yrtimd */
#include "transport.hpp"

#include <algorithm>
#include <thread>

#include <csconnector/csconnector.hpp>
#include <cscrypto/cscrypto.hpp>

#include <csnode/node.hpp>
#include <csnode/conveyer.hpp>
#include <csnode/configholder.hpp>

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

uint64_t getHostData() {
  auto platform = static_cast<uint8_t>(csconnector::connector::platform());
  auto node_version = static_cast<uint16_t>(cs::ConfigHolder::instance().config()->getNodeVersion());

  uint64_t user_data = 0;
  user_data |= platform;
  user_data |= (uint64_t(node_version) << 8);
  return user_data;
}

uint8_t getPlatform(uint64_t user_data) {
  return static_cast<uint8_t>(user_data);
}

uint16_t getVersion(uint64_t user_data) {
  return static_cast<uint16_t>(user_data >> 8);
}

net::Config createNetConfig(bool& good) {
    auto config = *cs::ConfigHolder::instance().config();
    net::Config result(toNodeId(config.getMyPublicKey()));
    good = true;

    auto& ep = config.getInputEndpoint();
    result.listen_address = !ep.ip.empty() ? ep.ip : net::kAllInterfaces;
    result.listen_port = ep.port ? ep.port : net::kDefaultPort;
    result.traverse_nat = config.traverseNAT();
#ifdef MONITOR_NODE
    result.full_net_discovery = true;
#endif
    result.host_data = getHostData();

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

// Extern function dfined in main.cpp to poll and handle signal status.
extern void pollSignalFlag();

Transport::Transport(Node* node)
: config_(createNetConfig(good_))
, node_(node)
, neighbourhood_(this, node_)
, host_(config_, static_cast<HostEventHandler&>(*this)) {
    cs::Connector::connect(&neighbourhood_.neighbourPingReceived, this, &Transport::onPingReceived);
}

Transport::~Transport() {
    if (processorThread_.joinable()) {
        processorThread_.join();
    }
}

void Transport::run() {
    host_.Run();
    processorThread_ = std::thread(&Transport::processorRoutine, this);
    std::this_thread::sleep_for(Neighbourhood::kPingInterval);

    while (Transport::gSignalStatus == 0) {
        pollSignalFlag();

        neighbourhood_.pingNeighbours();

        emit mainThreadIterated();
        std::this_thread::sleep_for(Neighbourhood::kPingInterval);
    }
}

void Transport::OnMessageReceived(const net::NodeId& id, net::ByteVector&& data) {
    Packet pack(std::move(data));
    if (!cs::PacketValidator::validate(pack)) {
        return;
    }

    auto publicKey = toPublicKey(id);
    if (pack.isNetwork()) {
        neighbourhood_.processNeighbourMessage(publicKey, pack);
        return;
    }

    {
        std::lock_guard g(inboxMux_);
        inboxQueue_.push(publicKey, std::move(pack));
    }

    newPacketsReceived_.notify_one();
}

void Transport::OnNodeDiscovered(const net::NodeId& id) {
    neighbourhood_.newPeerDiscovered(toPublicKey(id));
}

void Transport::OnNodeRemoved(const net::NodeId& id) {
    neighbourhood_.peerDisconnected(toPublicKey(id));
}

void Transport::onNeighboursChanged(const cs::PublicKey& neighbour, cs::Sequence lastSeq,
                                    cs::RoundNumber lastRound, bool added) {
    std::lock_guard<std::mutex> g(neighboursMux_);
    neighboursToHandle_.emplace_back(neighbour, lastSeq, lastRound, added);
}

void Transport::onPingReceived(cs::Sequence sequence, const cs::PublicKey& key) {
    cs::Concurrent::execute(cs::RunPolicy::CallQueuePolicy, [=] {
        emit pingReceived(sequence, key);
    });
}

void Transport::sendDirect(Packet&& pack, const cs::PublicKey& receiver) {
    host_.SendDirect(toNodeId(receiver), pack.moveData());
}

void Transport::ban(const cs::PublicKey& key) {
    host_.Ban(toNodeId(key));
}

void Transport::revertBan(const cs::PublicKey& key) {
    host_.Unban(toNodeId(key));
}

void Transport::clearBanList() {
    host_.ClearBanList();
}

void Transport::getBanList(BanList& ret_container) const {
    std::set<net::BanEntry> banned;
    host_.GetBanList(banned);

    for (auto& b : banned) {
        ret_container.push_back(std::make_pair(b.addr.to_string(), b.port));
    }
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

void Transport::sendBroadcastIfNoConnection(Packet&& pack, const cs::PublicKey& receiver) {
    host_.SendBroadcastIfNoConnection(toNodeId(receiver), pack.moveData());
}

void Transport::clearInbox() {
    {
        std::lock_guard g(inboxMux_);
        inboxQueue_.clear();
    }
}

void Transport::processorRoutine() {
    constexpr size_t kRoutineWaitTimeMs = 50;

    while (!node_->isStopRequested()) {
        process();

        std::unique_lock lock(inboxMux_);
        newPacketsReceived_.wait_for(lock, std::chrono::milliseconds{kRoutineWaitTimeMs}, [this]() {
            return !inboxQueue_.empty();
        });

        while (!inboxQueue_.empty()) {
            PacketsQueue::SenderAndPacket senderAndPack;
            try {
                senderAndPack = inboxQueue_.pop();
            } catch (...) {
              continue;
            }

            lock.unlock();

            process();
            processNodeMessage(senderAndPack.first, senderAndPack.second);

            lock.lock();
        }
    }
}

void Transport::process() {
    checkNeighboursChange();
    CallsQueue::instance().callAll();
}

void Transport::checkNeighboursChange() {
    std::lock_guard<std::mutex> lock(neighboursMux_);
    while (!neighboursToHandle_.empty()) {
        auto& neighbour = neighboursToHandle_.front();
        if (neighbour.added) {
            emit neighbourAdded(neighbour.key, neighbour.lastSeq, neighbour.lastRound);
        }
        else {
            emit neighbourRemoved(neighbour.key);
        }
        neighboursToHandle_.pop_front();
    }
}

void Transport::processNodeMessage(const cs::PublicKey& sender, const Packet& pack) {
    auto type = pack.getType();
    auto rNum = pack.getRoundNum();

    switch (node_->chooseMessageAction(rNum, type, sender)) {
        case Node::MessageActions::Process:
            return dispatchNodeMessage(sender, type, rNum, pack.getMsgData(), pack.getMsgSize());
        case Node::MessageActions::Postpone:
            return postponePacket(sender, rNum, pack);
        case Node::MessageActions::Drop:
            return;
    }
}

void Transport::dispatchNodeMessage(const cs::PublicKey& sender, const MsgTypes type, const cs::RoundNumber rNum, const uint8_t* data, size_t size) {
    // add special logs here
    switch (type) {
        case MsgTypes::ThirdSmartStage:
            csdebug() << "+++++++++++++++++++  ThirdSmartStage arrived +++++++++++++++++++++";
            break;
        default:
            break;
    }

    // call handlers
    switch (type) {
        case MsgTypes::BlockRequest:
            return node_->getBlockRequest(data, size, sender);
        case MsgTypes::RequestedBlock:
            return node_->getBlockReply(data, size, sender);
        case MsgTypes::Utility:
            return node_->getUtilityMessage(data, size);
        case MsgTypes::NodeStopRequest:
            return node_->getNodeStopRequest(rNum, data, size);
        case MsgTypes::RoundTable:
            return node_->getRoundTable(data, size, rNum, sender);
        case MsgTypes::BootstrapTable:
            return node_->getBootstrapTable(data, size, rNum);
        case MsgTypes::RoundTableRequest:
            return node_->getRoundTableRequest(data, size, rNum, sender);
        case MsgTypes::BlockHash:
            return node_->getHash(data, size, rNum, sender);
        case MsgTypes::HashReply:
            return node_->getHashReply(data, size, rNum, sender);
        case MsgTypes::TransactionPacket:
            return node_->getTransactionsPacket(data, size, sender);
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
            return node_->getStageThree(data, size, sender);
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
        case MsgTypes::BlockAlarm:
            return node_->getBlockAlarm(data, size, rNum, sender);
        case MsgTypes::EventReport:
            return node_->getEventReport(data, size, rNum, sender);
        case MsgTypes::SyncroMsg:
            return node_->getSyncroMessage(data, size, sender);
        case MsgTypes::TransactionPacketHash:
            return node_->getPacketHash(data, size, rNum, sender);
        default:
            break;
    }
}

inline void Transport::postponePacket(const cs::PublicKey& sender, const cs::RoundNumber rNum, const Packet& pack) {
    postponed_[rNum].push_back(PostponedPack{sender, pack});
}

void Transport::processPostponed(const cs::RoundNumber rNum) {
    auto& packs = postponed_[rNum];
    for (auto& p: packs) {
        dispatchNodeMessage(p.sender, p.pack.getType(), rNum, p.pack.getMsgData(), p.pack.getMsgSize());
    }

    postponed_.erase(postponed_.begin(), postponed_.upper_bound(rNum));
    csdebug() << "TRANSPORT> POSTPHONED finished, round " << rNum;
}

void Transport::setPermanentNeighbours(const std::set<cs::PublicKey>& neighbours) {
    neighbourhood_.setPermanentNeighbours(neighbours);
}

void Transport::forEachNeighbour(Neighbourhood::NeighboursCallback callback) {
    neighbourhood_.forEachNeighbour(callback);
}

uint32_t Transport::getNeighboursCount() const {
    return neighbourhood_.getNeighboursCount();
}

bool Transport::hasNeighbour(const cs::PublicKey& neighbour) const {
    return neighbourhood_.contains(neighbour);
}

uint32_t Transport::getMaxNeighbours() const {
    return Neighbourhood::kMaxNeighbours;
}

void Transport::getKnownPeers(std::vector<cs::PeerData>& result) {
    std::vector<net::NodeEntrance> knownPeers;
    host_.GetKnownNodes(knownPeers);

    for (auto& p : knownPeers) {
        cs::PeerData peerData;
        auto ptr = reinterpret_cast<const uint8_t*>(p.id.GetPtr());
        peerData.id = EncodeBase58(ptr, ptr + p.id.size());
        peerData.ip = p.address.to_string();
        peerData.port = p.udp_port;
        if (p.user_data) {
          peerData.version = static_cast<decltype(peerData.version)>(getVersion(p.user_data));
          peerData.platform = static_cast<decltype(peerData.platform)>(getPlatform(p.user_data));
        }

        result.push_back(peerData);
    }
}

void Transport::addToNeighbours(const std::set<cs::PublicKey>& keys) {
    neighbourhood_.add(keys);
}

net::FragmentId Transport::GetFragmentId(const net::ByteVector& fragment) {
  auto h = cscrypto::calculateHash(fragment.data(), fragment.size());
  net::FragmentId id;
  std::copy(h.begin(), h.end(), reinterpret_cast<uint8_t*>(id.GetPtr()));
  return id;
}
