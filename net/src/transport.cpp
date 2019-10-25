/* Send blaming letters to @yrtimd */
#include "transport.hpp"

#include <csnode/node.hpp>
#include <csnode/conveyer.hpp>
#include <csnode/packstream.hpp>

#include <lib/system/allocators.hpp>
#include <lib/system/utils.hpp>

#include <thread>

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

enum RegFlags : uint8_t {
    UsingIPv6 = 1,
    RedirectIP = 1 << 1,
    RedirectPort = 1 << 2
};

enum Platform : uint8_t {
    Linux,
    MacOS,
    Windows
};

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
, myPublicKey_(node->getNodeIdKey())
, oLock_()
, oPackStream_(&netPacksAllocator_, node->getNodeIdKey())
, node_(node) {
    good_ = true;
}

void Transport::run() {}

const char* Transport::networkCommandToString(NetworkCommand command) {
    switch (command) {
    case NetworkCommand::Registration:
        return "Registration";
    case NetworkCommand::ConfirmationRequest:
        return "ConfirmationRequest";
    case NetworkCommand::ConfirmationResponse:
        return "ConfirmationResponse";
    case NetworkCommand::RegistrationConfirmed:
        return "RegistrationConfirmed";
    case NetworkCommand::RegistrationRefused:
        return "RegistrationRefused";
    case NetworkCommand::Ping:
        return "Ping";
    case NetworkCommand::PackInform:
        return "PackInform";
    case NetworkCommand::PackRequest:
        return "PackRequest";
    case NetworkCommand::PackRenounce:
        return "PackRenounce";
    case NetworkCommand::BlockSyncRequest:
        return "BlockSyncRequest";
    case NetworkCommand::SSRegistration:
        return "SSRegistration";
    case NetworkCommand::SSFirstRound:
        return "SSFirstRound";
    case NetworkCommand::SSRegistrationRefused:
        return "SSRegistrationRefused";
    case NetworkCommand::SSPingWhiteNode:
        return "SSPingWhiteNode";
    case NetworkCommand::SSLastBlock:
        return "SSLastBlock";
    case NetworkCommand::SSReRegistration:
        return "SSReRegistration";
    case NetworkCommand::SSSpecificBlock:
        return "SSSpecificBlock";
    default:
        return "Unknown";
    }
}

template <>
uint16_t getHashIndex(const ip::udp::endpoint& ep) {
    uint16_t result = ep.port();

    if (ep.protocol() == ip::udp::v4()) {
        uint32_t address = ep.address().to_v4().to_uint();
        uint16_t lowBits = static_cast<uint16_t>(address);
        uint16_t highBits = address >> (sizeof(uint16_t) * CHAR_BIT);
        result ^= lowBits ^ highBits;
    }
    else {
        auto bytes = ep.address().to_v6().to_bytes();
        auto ptr = reinterpret_cast<uint8_t*>(&result);
        auto bytesPtr = bytes.data();
        for (size_t i = 0; i < 8; ++i) {
            *ptr ^= *(bytesPtr++);
        }

        ++ptr;

        for (size_t i = 8; i < 16; ++i) {
            *ptr ^= *(bytesPtr++);
        }
    }

    return result;
}

void Transport::deliverDirect(const Packet* pack, const uint32_t size, ConnectionPtr conn) {}

void Transport::deliverBroadcast(const Packet* pack, const uint32_t size) {}

bool Transport::checkConfidants(const std::vector<cs::PublicKey>& list, int except) {
/*    auto end = addresses_.end();
    int i = 0;
    for (const auto& pkey: list) {
        if (i++ == except) continue;
        if (addresses_.find(pkey) == end) return false;
    } */
    return true;
}

void Transport::deliverConfidants(const Packet* pack, const uint32_t size, const std::vector<cs::PublicKey>& list, int except) {}

// Processing network packages

void Transport::processNetworkTask(const TaskPtr<IPacMan>& task, RemoteNodePtr& sender) {
    iPackStream_.init(task->pack.getMsgData(), task->pack.getMsgSize());

    NetworkCommand cmd;
    iPackStream_ >> cmd;

    if (!iPackStream_.good()) {
        return sender->addStrike();
    }

    if (cmd != NetworkCommand::Registration) {
        if (sender->isBlackListed()) {
            csdebug() << "Network command is ignored from blacklisted " << task->sender;
            return;
        }
    }

    bool result = true;
    switch (cmd) {
        case NetworkCommand::Registration:
            result = gotRegistrationRequest(task, sender);
            break;
        case NetworkCommand::ConfirmationRequest:
            break;
        case NetworkCommand::ConfirmationResponse:
            break;
        case NetworkCommand::RegistrationConfirmed:
            result = gotRegistrationConfirmation(task, sender);
            break;
        case NetworkCommand::RegistrationRefused:
            result = gotRegistrationRefusal(task, sender);
            break;
        case NetworkCommand::Ping:
            gotPing(task, sender);
            break;
        case NetworkCommand::SSLastBlock: {
            long long timeSS{};
            iPackStream_ >> timeSS;
            node_->setDeltaTimeSS(timeSS);

//            gotSSLastBlock(task, node_->getBlockChain().getLastSeq(), node_->getBlockChain().getLastHash(),
//                node_->canBeTrusted(true /*crirical, all trusted required*/));
            break;
        }
        case NetworkCommand::SSSpecificBlock: {
            cs::RoundNumber round = 0;
            iPackStream_ >> round;

            long long timeSS{};
            iPackStream_ >> timeSS;
            node_->setDeltaTimeSS(timeSS);

            if (node_->getBlockChain().getLastSeq() < round) {
//                gotSSLastBlock(task, node_->getBlockChain().getLastSeq(), node_->getBlockChain().getLastHash(), false);
            }
            else {
//                gotSSLastBlock(task, round, node_->getBlockChain().getHashBySequence(round),
//                    node_->canBeTrusted(true /*crirical, all trusted required*/));
            }
            break;
        }
        case NetworkCommand::PackInform:
            gotPackInform(task, sender);
            break;
        case NetworkCommand::PackRenounce:
            // gotPackRenounce(task, sender);
            break;
        case NetworkCommand::PackRequest:
            // gotPackRequest(task, sender);
            break;
        default:
            result = false;
            cswarning() << "Unexpected network command";
    }

    if (!result) {
        sender->addStrike();
    }
}

bool Transport::gotRegistrationRefusal(const TaskPtr<IPacMan>& task, RemoteNodePtr&) {
    cslog() << "Got registration refusal from " << task->sender;

    RegistrationRefuseReasons reason;
    Connection::Id id;
    iPackStream_ >> id >> reason;

    if (!iPackStream_.good() || !iPackStream_.end()) {
        return false;
    }

    std::string reasonInfo = parseRefusalReason(reason);
    cslog() << "Registration to " << task->sender << " refused: " << reasonInfo;

    switch (reason) {
    case RegistrationRefuseReasons::BadClientVersion:
//        nh_.dropConnection(id);
        break;

    default:
//        nh_.gotRefusal(id);
        break;
    }

    return true;
}

bool Transport::gotPackInform(const TaskPtr<IPacMan>&, RemoteNodePtr& sender) {
    uint8_t isDirect = 0;
    cs::Hash hHash;
    iPackStream_ >> isDirect >> hHash;

    if (!iPackStream_.good() || !iPackStream_.end()) {
        return false;
    }

//    nh_.neighbourHasPacket(sender, hHash, isDirect);
    return true;
}


constexpr const uint32_t StrippedDataSize = sizeof(cs::RoundNumber) + sizeof(MsgTypes);
void Transport::processNodeMessage(const Message& msg) {
    auto type = msg.getFirstPack().getType();
    auto rNum = msg.getFirstPack().getRoundNum();

    switch (node_->chooseMessageAction(rNum, type, msg.getFirstPack().getSender())) {
        case Node::MessageActions::Process:
            return dispatchNodeMessage(type, rNum, msg.getFirstPack(), msg.getFullData() + StrippedDataSize, msg.getFullSize() - StrippedDataSize);
        case Node::MessageActions::Postpone:
            return postponePacket(rNum, type, msg.extractData());
        case Node::MessageActions::Drop:
            return;
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

bool Transport::shouldSendPacket(const Packet& pack) {
    if (pack.isNetwork()) {
        return false;
    }

    const cs::RoundNumber currentRound = cs::Conveyer::instance().currentRoundNumber();

    if (!pack.isFragmented()) {
        return (pack.getRoundNum() + getRoundTimeout(pack.getType())) >= currentRound;
    }

//    auto& rn = fragOnRound_.tryStore(pack.getHeaderHash());
    cs::RoundNumber rn;

    if (pack.getFragmentId() == 0) {
        rn = pack.getRoundNum() + getRoundTimeout(pack.getType());
    }

    return !rn || rn >= currentRound;
}

void Transport::processNodeMessage(const Packet& pack) {
    auto type = pack.getType();
    auto rNum = pack.getRoundNum();

    switch (node_->chooseMessageAction(rNum, type, pack.getSender())) {
        case Node::MessageActions::Process:
            return dispatchNodeMessage(type, rNum, pack, pack.getMsgData() + StrippedDataSize, pack.getMsgSize() - StrippedDataSize);
        case Node::MessageActions::Postpone:
            return postponePacket(rNum, type, pack);
        case Node::MessageActions::Drop:
            return;
    }
}

inline void Transport::postponePacket(const cs::RoundNumber rNum, const MsgTypes type, const Packet& pack) {
    (*postponed_)->emplace(rNum, type, pack);
}

void Transport::processPostponed(const cs::RoundNumber rNum) {
    auto& ppBuf = *postponed_[1];
    for (auto& pp : **postponed_) {
        if (pp.round > rNum) {
            ppBuf.emplace(std::move(pp));
        }
        else if (pp.round == rNum) {
            dispatchNodeMessage(pp.type, pp.round, pp.pack, pp.pack.getMsgData() + StrippedDataSize, pp.pack.getMsgSize() - StrippedDataSize);
        }
    }

    (*postponed_)->clear();

    postponed_[1] = *postponed_;
    postponed_[0] = &ppBuf;

    csdebug() << "TRANSPORT> POSTPHONED finished, round " << rNum;
}

void Transport::dispatchNodeMessage(const MsgTypes type, const cs::RoundNumber rNum, const Packet& firstPack, const uint8_t* data, size_t size) {
    if (size == 0) {
        cserror() << "Bad packet size, why is it zero?";
        return;
    }

    // cut my packs
    if (firstPack.getSender() == node_->getNodeIdKey()) {
        csdebug() << "TRANSPORT> Ignore own packs";
        return;
    }

    // never cut packets
    switch (type) {
        case MsgTypes::BlockRequest:
            return node_->getBlockRequest(data, size, firstPack.getSender());
        case MsgTypes::RequestedBlock:
            return node_->getBlockReply(data, size);
        case MsgTypes::BigBang:  // any round (in theory) may be set
            return node_->getBigBang(data, size, rNum);
        case MsgTypes::RoundTableRequest:  // old-round node may ask for round info
            return node_->getRoundTableRequest(data, size, rNum, firstPack.getSender());
        case MsgTypes::NodeStopRequest:
            return node_->getNodeStopRequest(rNum, data, size);
        case MsgTypes::RoundTable:
            return node_->getRoundTable(data, size, rNum, firstPack.getSender());
        case MsgTypes::RoundTableSS:
            return node_->getRoundTableSS(data, size, rNum);
        default:
            break;
    }

    // cut slow packs
    if ((rNum + getRoundTimeout(type)) < cs::Conveyer::instance().currentRoundNumber()) {
        csdebug() << "TRANSPORT> Ignore old packs, round " << rNum << ", type " << Packet::messageTypeToString(type) << ", fragments " << firstPack.getFragmentsNum();
        return;
    }

    if (type == MsgTypes::ThirdSmartStage) {
        csdebug() << "+++++++++++++++++++  ThirdSmartStage arrived +++++++++++++++++++++";
    }

    // packets which transport may cut
    switch (type) {
        case MsgTypes::BlockHash:
            return node_->getHash(data, size, rNum, firstPack.getSender());
        case MsgTypes::HashReply:
            return node_->getHashReply(data, size, rNum, firstPack.getSender());
        case MsgTypes::TransactionPacket:
            return node_->getTransactionsPacket(data, size);
        case MsgTypes::TransactionsPacketRequest:
            return node_->getPacketHashesRequest(data, size, rNum, firstPack.getSender());
        case MsgTypes::TransactionsPacketReply:
            return node_->getPacketHashesReply(data, size, rNum, firstPack.getSender());
        case MsgTypes::FirstStage:
            return node_->getStageOne(data, size, firstPack.getSender());
        case MsgTypes::SecondStage:
            return node_->getStageTwo(data, size, firstPack.getSender());
        case MsgTypes::FirstStageRequest:
            return node_->getStageRequest(type, data, size, firstPack.getSender());
        case MsgTypes::SecondStageRequest:
            return node_->getStageRequest(type, data, size, firstPack.getSender());
        case MsgTypes::ThirdStageRequest:
            return node_->getStageRequest(type, data, size, firstPack.getSender());
        case MsgTypes::ThirdStage:
            return node_->getStageThree(data, size);
        case MsgTypes::FirstSmartStage:
            return node_->getSmartStageOne(data, size, rNum, firstPack.getSender());
        case MsgTypes::SecondSmartStage:
            return node_->getSmartStageTwo(data, size, rNum, firstPack.getSender());
        case MsgTypes::ThirdSmartStage:
            return node_->getSmartStageThree(data, size, rNum, firstPack.getSender());
        case MsgTypes::SmartFirstStageRequest:
            return node_->getSmartStageRequest(type, data, size, firstPack.getSender());
        case MsgTypes::SmartSecondStageRequest:
            return node_->getSmartStageRequest(type, data, size, firstPack.getSender());
        case MsgTypes::SmartThirdStageRequest:
            return node_->getSmartStageRequest(type, data, size, firstPack.getSender());
        case MsgTypes::RejectedContracts:
            return node_->getSmartReject(data, size, rNum, firstPack.getSender());
        case MsgTypes::RoundTableReply:
            return node_->getRoundTableReply(data, size, firstPack.getSender());
        case MsgTypes::RoundPackRequest:
            return node_->getRoundPackRequest(data, size, rNum, firstPack.getSender());
        case MsgTypes::EmptyRoundPack:
            return node_->getEmptyRoundPack(data, size, rNum, firstPack.getSender());
        case MsgTypes::StateRequest:
            return node_->getStateRequest(data, size, rNum, firstPack.getSender());
        case MsgTypes::StateReply:
            return node_->getStateReply(data, size, rNum, firstPack.getSender());
        default:
            cserror() << "TRANSPORT> Unknown message type " << Packet::messageTypeToString(type) << " pack round " << rNum;
            break;
    }
}

uint32_t Transport::getNeighboursCount() {
    return 0;
}

uint32_t Transport::getMaxNeighbours() const {
    return config_->getMaxNeighbours();
}

ConnectionPtr Transport::getConnectionByKey(const cs::PublicKey& pk) {
    return ConnectionPtr();
}

ConnectionPtr Transport::getConnectionByNumber(const std::size_t number) {
    return ConnectionPtr();
}

cs::Sequence Transport::getConnectionLastSequence(const std::size_t number) {
/*    ConnectionPtr ptr = getConnectionByNumber(number);
    if (ptr && !ptr->isSignal) {
        return ptr->lastSeq;
    } */
    return cs::Sequence{};
}

/*bool Transport::isShouldUpdateNeighbours() const {
    return nh_.getNeighboursCountWithoutSS() < config_->getMinNeighbours();
}*/

void Transport::onConfigChanged(const Config& updated) {
    config_.exchange(updated);
}

void Transport::addMyOut(const uint8_t initFlagValue) {
    uint8_t regFlag = 0;
    if (!config_->isSymmetric()) {
        if (config_->getAddressEndpoint().ipSpecified) {
            regFlag |= RegFlags::RedirectIP;
            if (config_->getAddressEndpoint().ip.is_v6()) {
                regFlag |= RegFlags::UsingIPv6;
            }
        }

        regFlag |= RegFlags::RedirectPort;
    }
    else if (config_->hasTwoSockets()) {
        regFlag |= RegFlags::RedirectPort;
    }

    uint8_t* flagChar = oPackStream_.getCurrentPtr();

    if (!config_->isSymmetric()) {
        if (config_->getAddressEndpoint().ipSpecified) {
            oPackStream_ << config_->getAddressEndpoint().ip;
        }
        else {
            uint8_t c = 0_b;
            oPackStream_ << c;
        }

        oPackStream_ << config_->getAddressEndpoint().port;
    }
    else if (config_->hasTwoSockets()) {
        oPackStream_ << 0_b << config_->getInputEndpoint().port;
    }
    else {
        oPackStream_ << 0_b;
    }

    *flagChar |= initFlagValue | regFlag;
}

void Transport::formRegPack(uint64_t** regPackConnId, const cs::PublicKey& pk, uint64_t uuid) {
    oPackStream_.init(BaseFlags::NetworkMsg);
    oPackStream_ << NetworkCommand::Registration << NODE_VERSION << uuid;

    addMyOut();
    *regPackConnId = reinterpret_cast<uint64_t*>(oPackStream_.getCurrentPtr());

    oPackStream_ << static_cast<ConnectionId>(0) << pk;
}

/* Sending network tasks */
void Transport::sendRegistrationRequest(Connection& conn) {
/*    RemoteNodePtr ptr = getPackSenderEntry(conn.getOut());

    if (ptr->isBlackListed()) {
        return;
    }

    cslog() << "Sending registration request to " << (conn.specialOut ? conn.out : conn.in);

    cs::Lock lock(oLock_);
    Packet req(netPacksAllocator_.allocateNext(cs::numeric_cast<uint32_t>(regPack_.size())));
    *regPackConnId_ = conn.id;
    std::memcpy(req.data(), regPack_.data(), regPack_.size());

    ++(conn.attempts);
    sendDirect(&req, conn); */
}

void Transport::sendRegistrationConfirmation(const Connection& conn, const Connection::Id requestedId) {
//    cslog() << "Confirming registration with " << conn.getOut();

    cs::Lock lock(oLock_);
    oPackStream_.init(BaseFlags::NetworkMsg);
    oPackStream_ << NetworkCommand::RegistrationConfirmed << requestedId << conn.id << myPublicKey_;

//    sendDirect(oPackStream_.getPackets(), conn);
    oPackStream_.clear();
}

void Transport::sendRegistrationRefusal(const Connection& conn, const RegistrationRefuseReasons reason) {
//    cslog() << "Refusing registration with " << conn.in << " reason: " << parseRefusalReason(reason);

    cs::Lock lock(oLock_);
    oPackStream_.init(BaseFlags::NetworkMsg);
    oPackStream_ << NetworkCommand::RegistrationRefused << conn.id << reason;

//    sendDirect(oPackStream_.getPackets(), conn);
    oPackStream_.clear();
}

// Requests processing
bool Transport::gotRegistrationRequest(const TaskPtr<IPacMan>& task, RemoteNodePtr& sender) {
    cslog() << "Got registration request from " << task->sender;

    NodeVersion version;
    uint64_t remoteUuid = 0;
    iPackStream_ >> version >> remoteUuid;

    if (!iPackStream_.good()) {
        return false;
    }

    Connection conn;
    conn.version = version;

    auto& flags = iPackStream_.peek<uint8_t>();

    if (flags & RegFlags::RedirectIP) {
        boost::asio::ip::address addr;
        iPackStream_ >> addr;
    }
    else {
        iPackStream_.skip<uint8_t>();
    }

    if (flags & RegFlags::RedirectPort) {
        Port port = Port();
        iPackStream_ >> port;
    }

    if (version < config_->getMinCompatibleVersion()) {
        sendRegistrationRefusal(conn, RegistrationRefuseReasons::BadClientVersion);
        return true;
    }

    RemoteNodePtr ptr;// = getPackSenderEntry(conn.getOut());
    uint64_t uuid = node_->getBlockChain().uuid();

    if (uuid != 0 && remoteUuid != 0 && uuid != remoteUuid) {
       sendRegistrationRefusal(conn, RegistrationRefuseReasons::IncompatibleBlockchain);
       ptr->setBlackListed(true);
       return true;
    }
    else {
       ptr->setBlackListed(false);
    }

    iPackStream_ >> conn.id;
    iPackStream_ >> conn.key;

    if (!iPackStream_.good() || !iPackStream_.end()) {
        return false;
    }

//    nh_.gotRegistration(std::move(conn), sender);
    return true;
}

bool Transport::gotRegistrationConfirmation(const TaskPtr<IPacMan>& task, RemoteNodePtr& sender) {
    cslog() << "Got registration confirmation from " << task->sender;

    ConnectionId myCId;
    ConnectionId realCId;
    cs::PublicKey key;
    iPackStream_ >> myCId >> realCId >> key;

    if (!iPackStream_.good()) {
        return false;
    }

//    nh_.gotConfirmation(myCId, realCId, task->sender, key, sender);
    return true;
}

// Turn on testing blockchain ID in PING packets to prevent nodes from confuse alien ones
#define PING_WITH_BCHID

void Transport::sendPingPack(const Connection& conn) {
    cs::Sequence seq = node_->getBlockChain().getLastSeq();
    cs::Lock lock(oLock_);
    oPackStream_.init(BaseFlags::NetworkMsg);
    oPackStream_ << NetworkCommand::Ping << conn.id << seq << myPublicKey_;

#if defined(PING_WITH_BCHID)
    oPackStream_ << node_->getBlockChain().uuid();
#endif

    if (!config_->isCompatibleVersion()) {
        oPackStream_ << NODE_VERSION;
    }

//    sendDirect(oPackStream_.getPackets(), conn);
    oPackStream_.clear();
}

bool Transport::gotPing(const TaskPtr<IPacMan>& task, RemoteNodePtr& sender) {
    Connection::Id id = 0u;
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
}
