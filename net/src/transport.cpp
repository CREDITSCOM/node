/* Send blaming letters to @yrtimd */
#include <csnode/conveyer.hpp>
#include <csnode/packstream.hpp>

#include <lib/system/allocators.hpp>
#include <lib/system/utils.hpp>

#include "network.hpp"
#include "transport.hpp"

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

namespace {
// Packets formation

void addMyOut(const Config& config, cs::OPackStream& stream, const uint8_t initFlagValue = 0) {
    uint8_t regFlag = 0;
    if (!config.isSymmetric()) {
        if (config.getAddressEndpoint().ipSpecified) {
            regFlag |= RegFlags::RedirectIP;
            if (config.getAddressEndpoint().ip.is_v6()) {
                regFlag |= RegFlags::UsingIPv6;
            }
        }

        regFlag |= RegFlags::RedirectPort;
    }
    else if (config.hasTwoSockets()) {
        regFlag |= RegFlags::RedirectPort;
    }

    uint8_t* flagChar = stream.getCurrentPtr();

    if (!config.isSymmetric()) {
        if (config.getAddressEndpoint().ipSpecified) {
            stream << config.getAddressEndpoint().ip;
        }
        else {
            uint8_t c = 0_b;
            stream << c;
        }

        stream << config.getAddressEndpoint().port;
    }
    else if (config.hasTwoSockets()) {
        stream << 0_b << config.getInputEndpoint().port;
    }
    else {
        stream << 0_b;
    }

    *flagChar |= initFlagValue | regFlag;
}

void formRegPack(const Config& config, cs::OPackStream& stream, uint64_t** regPackConnId, const cs::PublicKey& pk, uint64_t uuid) {
    stream.init(BaseFlags::NetworkMsg);
    stream << NetworkCommand::Registration << NODE_VERSION << uuid;

    addMyOut(config, stream);
    *regPackConnId = reinterpret_cast<uint64_t*>(stream.getCurrentPtr());

    stream << static_cast<ConnectionId>(0) << pk;
}

void formSSConnectPack(const Config& config, cs::OPackStream& stream, const cs::PublicKey& pk, uint64_t uuid) {
    stream.init(BaseFlags::NetworkMsg);
    stream << NetworkCommand::SSRegistration
#ifdef _WIN32
        << Platform::Windows
#elif __APPLE__
        << Platform::MacOS
#else
        << Platform::Linux
#endif
        << NODE_VERSION << uuid;

    uint8_t flag = (config.getNodeType() == NodeType::Router) ? 8 : 0;
    addMyOut(config, stream, flag);

    stream << pk;
}
}  // namespace

Transport::~Transport() {
    delete net_;
}

void Transport::run() {
    net_->sendInit();
    acceptRegistrations_ = config_.getNodeType() == NodeType::Router;

    {
        cs::Lock lock(oLock_);
        oPackStream_.init(BaseFlags::NetworkMsg);
        formRegPack(config_, oPackStream_, &regPackConnId_, myPublicKey_, node_->getBlockChain().uuid());
        regPack_ = *(oPackStream_.getPackets());
        oPackStream_.clear();
    }

    refillNeighbourhood();

    // Okay, now let's get to business
    uint32_t ctr = 0;
    cswarning() << "+++++++>>> Transport Run Task Start <<<+++++++++++++++";

    // Check if thread is requested to stop ?
    while (Transport::gSignalStatus == 0) {
        ++ctr;

        bool askMissing = true;
        bool resendPacks = ctr % 10 == 0;
        bool sendPing = ctr % 20 == 0;
        bool refreshLimits = ctr % 20 == 0;
        bool checkPending = ctr % 100 == 0;
        bool checkSilent = ctr % 150 == 0;

        if (askMissing) {
            askForMissingPackages();
        }

        if (checkPending) {
            nh_.checkPending(config_.getMaxNeighbours());
        }

        if (checkSilent) {
            nh_.checkSilent();
            nh_.checkNeighbours();
        }

        if (resendPacks) {
            nh_.resendPackets();
        }

        if (sendPing) {
            nh_.pingNeighbours();
        }

        if (refreshLimits) {
            nh_.refreshLimits();
        }

        pollSignalFlag();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    cswarning() << "[Transport::run STOPED!]";
}

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

RemoteNodePtr Transport::getPackSenderEntry(const ip::udp::endpoint& ep) {
    auto& rn = remoteNodesMap_.tryStore(ep);

    if (!rn) {  // Newcomer
        rn = remoteNodes_.emplace();
    }

    rn->packets.fetch_add(1, std::memory_order_relaxed);
    return rn;
}

bool Transport::sendDirect(const Packet* pack, const Connection& conn) {
    uint32_t nextBytesCount = static_cast<uint32_t>(conn.lastBytesCount.load(std::memory_order_relaxed) + pack->size());
    if (nextBytesCount <= config_.getConnectionBandwidth()) {
        conn.lastBytesCount.fetch_add(static_cast<uint32_t>(pack->size()), std::memory_order_relaxed);
        net_->sendDirect(*pack, conn.getOut());
        return true;
    }

    return false;
}

void Transport::deliverDirect(const Packet* pack, const uint32_t size, ConnectionPtr conn) {
    if (size >= Packet::MaxFragments) {
        ++Transport::cntExtraLargeNotSent;
        return;
    }
    const auto packEnd = pack + size;
    for (auto ptr = pack; ptr != packEnd; ++ptr) {
        nh_.registerDirect(ptr, conn);
        sendDirect(ptr, **conn);
    }
}

void Transport::deliverBroadcast(const Packet* pack, const uint32_t size) {
    if (size >= Packet::MaxFragments) {
        ++Transport::cntExtraLargeNotSent;
        return;
    }
    const auto packEnd = pack + size;
    for (auto ptr = pack; ptr != packEnd; ++ptr) {
        sendBroadcast(ptr);
    }
}

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
        case NetworkCommand::SSRegistration:
            gotSSRegistration(task, sender);
            break;
        case NetworkCommand::SSReRegistration:
            gotSSReRegistration();
            break;
        case NetworkCommand::SSFirstRound:
            gotSSDispatch(task);
            break;
        case NetworkCommand::SSRegistrationRefused:
            gotSSRefusal(task);
            break;
        case NetworkCommand::SSPingWhiteNode:
            gotSSPingWhiteNode(task);
            break;
        case NetworkCommand::SSLastBlock:
            gotSSLastBlock(task, node_->getBlockChain().getLastSequence(), node_->getBlockChain().getLastHash(), node_->canBeTrusted());
            break;
        case NetworkCommand::SSSpecificBlock: {
                cs::RoundNumber round = 0;
                iPackStream_ >> round;

            if (node_->getBlockChain().getLastSequence() < round) {
                gotSSLastBlock(task, node_->getBlockChain().getLastSequence(), node_->getBlockChain().getLastHash(), false);
            }
            else {
                gotSSLastBlock(task, round, node_->getBlockChain().getHashBySequence(round), node_->canBeTrusted());
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

void Transport::refillNeighbourhood() {
    // TODO: check this algorithm when all list nodes are dead
    if (config_.getBootstrapType() == BootstrapType::IpList) {
        for (auto& ep : config_.getIpList()) {
            if (!nh_.canHaveNewConnection()) {
                cswarning() << "Connections limit reached";
                break;
            }

            cslog() << "Creating connection to " << ep.ip;
            nh_.establishConnection(net_->resolve(ep));
        }
    }

    if (config_.getBootstrapType() == BootstrapType::SignalServer || config_.getNodeType() == NodeType::Router) {
        // Connect to SS logic
        ssEp_ = net_->resolve(config_.getSignalServerEndpoint());
        cslog() << "Connecting to Signal Server on " << ssEp_;

        {
            cs::Lock lock(oLock_);
            formSSConnectPack(config_, oPackStream_, myPublicKey_, node_->getBlockChain().uuid());
            ssStatus_ = SSBootstrapStatus::Requested;
            net_->sendDirect(*(oPackStream_.getPackets()), ssEp_);
        }
    }
}

bool Transport::parseSSSignal(const TaskPtr<IPacMan>& task) {
    iPackStream_.init(task->pack.getMsgData(), task->pack.getMsgSize());
    iPackStream_.safeSkip<uint8_t>(1);
    iPackStream_.safeSkip<cscrypto::PublicKey>(1);

    cs::RoundNumber rNum = 0;
    iPackStream_ >> rNum;

    auto trStart = iPackStream_.getCurrentPtr();

    uint8_t numConf;
    iPackStream_ >> numConf;

    if (!iPackStream_.good()) {
        return false;
    }

    iPackStream_.safeSkip<cs::PublicKey>(numConf + 1);

    auto trFinish = iPackStream_.getCurrentPtr();
    node_->getRoundTableSS(trStart, cs::numeric_cast<size_t>(trFinish - trStart), rNum);

    uint8_t numCirc;
    iPackStream_ >> numCirc;

    if (!iPackStream_.good()) {
        return false;
    }

    uint32_t ctr = nh_.size();

    if (config_.getBootstrapType() == BootstrapType::SignalServer) {
        for (uint8_t i = 0; i < numCirc; ++i) {
            EndpointData ep;
            ep.ipSpecified = true;
            cs::PublicKey key;

            iPackStream_ >> ep.ip >> ep.port >> key;

            if (!iPackStream_.good()) {
                return false;
            }

            ++ctr;

            if (!std::equal(key.cbegin(), key.cend(), config_.getMyPublicKey().cbegin())) {
                if (ctr <= config_.getMaxNeighbours()) {
                    nh_.establishConnection(net_->resolve(ep));
                }
            }

            if (!nh_.canHaveNewConnection()) {
                break;
            }
        }
    }

    ssStatus_ = SSBootstrapStatus::Complete;
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

    auto& rn = fragOnRound_.tryStore(pack.getHeaderHash());

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
        default:
            cserror() << "TRANSPORT> Unknown message type " << Packet::messageTypeToString(type) << " pack round " << rNum;
            break;
    }
}

void Transport::registerTask(Packet* pack, const uint32_t packNum, const bool incrementWhenResend) {
    auto end = pack + packNum;

    for (auto ptr = pack; ptr != end; ++ptr) {
        cs::Lock lock(sendPacksFlag_);
        PackSendTask pst;
        pst.pack = *ptr;
        pst.incrementId = incrementWhenResend;
        sendPacks_.emplace(pst);
    }
}

void Transport::addTask(Packet* pack, const uint32_t packNum, bool incrementWhenResend) {
    if (packNum >= Packet::MaxFragments) {
        ++Transport::cntExtraLargeNotSent;
        return;
    }
    nh_.pourByNeighbours(pack, packNum);
    if (packNum > 1) {
        net_->registerMessage(pack, packNum);
    }
    registerTask(pack, 1, incrementWhenResend);
}

void Transport::clearTasks() {
    cs::Lock lock(sendPacksFlag_);
    sendPacks_.clear();
}

uint32_t Transport::getNeighboursCount() {
    return nh_.size();
}

uint32_t Transport::getNeighboursCountWithoutSS() {
    return nh_.getNeighboursCountWithoutSS();
}

uint32_t Transport::getMaxNeighbours() const {
    return config_.getMaxNeighbours();
}

ConnectionPtr Transport::getSyncRequestee(const cs::Sequence seq, bool& alreadyRequested) {
    return nh_.getNextSyncRequestee(seq, alreadyRequested);
}

ConnectionPtr Transport::getConnectionByKey(const cs::PublicKey& pk) {
    return nh_.getNeighbourByKey(pk);
}

ConnectionPtr Transport::getConnectionByNumber(const std::size_t number) {
    return nh_.getNeighbour(number);
}

ConnectionPtr Transport::getRandomNeighbour() {
    csdebug() << "Transport> Get random neighbour for Sync";
    return nh_.getRandomSyncNeighbour();
}

std::unique_lock<cs::SpinLock> Transport::getNeighboursLock() const {
    return nh_.getNeighboursLock();
}

void Transport::forEachNeighbour(std::function<void(ConnectionPtr)> func) {
    nh_.forEachNeighbour(std::move(func));
}

void Transport::forEachNeighbourWithoudSS(std::function<void(ConnectionPtr)> func) {
    nh_.forEachNeighbourWithoutSS(std::move(func));
}

const Connections Transport::getNeighbours() const {
    return nh_.getNeigbours();
}

const Connections Transport::getNeighboursWithoutSS() const {
    return nh_.getNeighboursWithoutSS();
}

void Transport::syncReplied(const cs::Sequence seq) {
    return nh_.releaseSyncRequestee(seq);
}

bool Transport::isPingDone() {
    return nh_.isPingDone();
}

void Transport::resetNeighbours() {
    csdebug() << "Transport> Reset neighbours";
    return nh_.resetSyncNeighbours();
}

/* Sending network tasks */
void Transport::sendRegistrationRequest(Connection& conn) {

    RemoteNodePtr ptr = getPackSenderEntry(conn.getOut());
    if (ptr->isBlackListed()) {
        return;
    }

    cslog() << "Sending registration request to " << (conn.specialOut ? conn.out : conn.in);

    cs::Lock lock(oLock_);
    Packet req(netPacksAllocator_.allocateNext(cs::numeric_cast<uint32_t>(regPack_.size())));
    *regPackConnId_ = conn.id;
    memcpy(req.data(), regPack_.data(), regPack_.size());

    ++(conn.attempts);
    sendDirect(&req, conn);
}

void Transport::sendRegistrationConfirmation(const Connection& conn, const Connection::Id requestedId) {
    cslog() << "Confirming registration with " << conn.getOut();

    cs::Lock lock(oLock_);
    oPackStream_.init(BaseFlags::NetworkMsg);
    oPackStream_ << NetworkCommand::RegistrationConfirmed << requestedId << conn.id << myPublicKey_;

    sendDirect(oPackStream_.getPackets(), conn);
    oPackStream_.clear();
}

void Transport::sendRegistrationRefusal(const Connection& conn, const RegistrationRefuseReasons reason) {
    cslog() << "Refusing registration with " << conn.in;

    cs::Lock lock(oLock_);
    oPackStream_.init(BaseFlags::NetworkMsg);
    oPackStream_ << NetworkCommand::RegistrationRefused << conn.id << reason;

    sendDirect(oPackStream_.getPackets(), conn);
    oPackStream_.clear();
}

// Requests processing
bool Transport::gotRegistrationRequest(const TaskPtr<IPacMan>& task, RemoteNodePtr& sender) {
    cslog() << "Got registration request from " << task->sender;

    NodeVersion vers;
    uint64_t remote_uuid = 0;
    iPackStream_ >> vers >> remote_uuid;

    if (!iPackStream_.good()) {
        return false;
    }

    Connection conn;
    conn.in = task->sender;
    auto& flags = iPackStream_.peek<uint8_t>();

    if (flags & RegFlags::RedirectIP) {
        boost::asio::ip::address addr;
        iPackStream_ >> addr;

        conn.out.address(addr);
        conn.specialOut = true;
    }
    else {
        conn.specialOut = false;
        iPackStream_.skip<uint8_t>();
    }

    if (flags & RegFlags::RedirectPort) {
        Port port = Port();
        iPackStream_ >> port;

        if (!conn.specialOut) {
            conn.specialOut = true;
            conn.out.address(task->sender.address());
        }
        conn.out.port(port);
    }
    else if (conn.specialOut) {
        conn.out.port(task->sender.port());
    }

    if (vers != NODE_VERSION) {
        sendRegistrationRefusal(conn, RegistrationRefuseReasons::BadClientVersion);
        return true;
    }

    RemoteNodePtr ptr = getPackSenderEntry(conn.getOut());
    uint64_t local_uuid = node_->getBlockChain().uuid();
    if (local_uuid != 0 && remote_uuid != 0 && local_uuid != remote_uuid) {
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

    nh_.gotRegistration(std::move(conn), sender);
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

    nh_.gotConfirmation(myCId, realCId, task->sender, key, sender);
    return true;
}

bool Transport::gotRegistrationRefusal(const TaskPtr<IPacMan>& task, RemoteNodePtr&) {
    cslog() << "Got registration refusal from " << task->sender;

    RegistrationRefuseReasons reason;
    Connection::Id id;
    iPackStream_ >> id >> reason;

    if (!iPackStream_.good() || !iPackStream_.end()) {
        return false;
    }

    nh_.gotRefusal(id);

    std::string reason_info;
    switch (reason) {
    case RegistrationRefuseReasons::BadClientVersion:
        reason_info = "incompatible node version";
        break;
    case RegistrationRefuseReasons::IncompatibleBlockchain:
        reason_info = "incompatible blockchain version";
        break;
    case RegistrationRefuseReasons::LimitReached:
        reason_info = "maximum connections limit on remote node is reached";
        break;
    default:
        {
            std::ostringstream os;
            os << "reason code " << static_cast<int>(reason);
            reason_info = os.str();
        }
        break;
    }
    cslog() << "Registration to " << task->sender << " refused: " << reason_info;

    return true;
}

bool Transport::gotSSRegistration(const TaskPtr<IPacMan>& task, RemoteNodePtr& rNode) {
    if (ssStatus_ != SSBootstrapStatus::Requested) {
        cswarning() << "Unexpected Signal Server response " << static_cast<int>(ssStatus_) << " instead of Requested";
        return false;
    }

    cslog() << "Connection to the Signal Server has been established";
    nh_.addSignalServer(task->sender, ssEp_, rNode);

    constexpr int MinRegistrationSize = 1 + cscrypto::kPublicKeySize;
    size_t msg_size = task->pack.getMsgSize();

    if (msg_size > MinRegistrationSize) {
        if (!parseSSSignal(task)) {
            cswarning() << "Bad Signal Server response";
        }
    }
    else {
        ssStatus_ = SSBootstrapStatus::RegisteredWait;
    }

    return true;
}

bool Transport::gotSSReRegistration() {
    cswarning() << "ReRegistration on Signal Server";

    {
        cs::Lock lock(oLock_);
        formSSConnectPack(config_, oPackStream_, myPublicKey_, node_->getBlockChain().uuid());
        net_->sendDirect(*(oPackStream_.getPackets()), ssEp_);
    }

    return true;
}

bool Transport::gotSSDispatch(const TaskPtr<IPacMan>& task) {
    if (ssStatus_ != SSBootstrapStatus::RegisteredWait) {
        cswarning() << "Unexpected Signal Server response " << static_cast<int>(ssStatus_) << " instead of RegisteredWait";
    }

    if (!parseSSSignal(task)) {
        cswarning() << "Bad Signal Server response";
    }

    return true;
}

bool Transport::gotSSRefusal(const TaskPtr<IPacMan>&) {
    uint16_t expectedVersion;
    RegistrationRefuseReasons reason;
    iPackStream_ >> expectedVersion >> reason;

    if (!iPackStream_.good() || !iPackStream_.end()) {
        return false;
    }

    cserror() << "The Starter node has refused the registration";
    switch (reason) {
    case RegistrationRefuseReasons::BadClientVersion:
        cserror() << "Your client version << " << NODE_VERSION << " is obsolete. The allowed version is " << expectedVersion << ". Please upgrade your node application";
        break;
    case RegistrationRefuseReasons::IncompatibleBlockchain:
        cserror() << "Your blockchain version is incompatible. You only may join the network with empty blockchain";
        break;
    default:
        cserror() << "The reason code is " << static_cast<int>(reason);
        break;
    }

    return true;
}

bool Transport::gotSSPingWhiteNode(const TaskPtr<IPacMan>& task) {
    Connection conn;
    conn.in = task->sender;
    conn.specialOut = false;
    sendDirect(&task->pack, conn);
    return true;
}

bool Transport::gotSSLastBlock(const TaskPtr<IPacMan>& task, cs::Sequence lastBlock, const csdb::PoolHash& lastHash, bool canBeTrusted) {
#if !defined(MONITOR_NODE) && !defined(WEB_WALLET_NODE)
    csdebug() << "TRANSPORT> Got SS Last Block: " << lastBlock;
    csunused(task);

    Connection conn;
    conn.in = net_->resolve(config_.getSignalServerEndpoint());
    conn.specialOut = false;

    cs::Lock lock(oLock_);
    oPackStream_.init(BaseFlags::NetworkMsg);
    oPackStream_ << NetworkCommand::SSLastBlock << NODE_VERSION;

    cs::Hash lastHash_;
    const auto hashBinary = lastHash.to_binary();
    std::copy(hashBinary.begin(), hashBinary.end(), lastHash_.begin());

    oPackStream_ << lastBlock << canBeTrusted << myPublicKey_ << lastHash_;

    sendDirect(oPackStream_.getPackets(), conn);
#else
    csunused(task);
    csunused(lastBlock);
    csunused(lastHash);
    csunused(canBeTrusted);
#endif
    return true;
}

void Transport::gotPacket(const Packet& pack, RemoteNodePtr& sender) {
    if (!pack.isFragmented()) {
        return;
    }

    nh_.neighbourSentPacket(sender, pack.getHeaderHash());
}

void Transport::redirectPacket(const Packet& pack, RemoteNodePtr& sender) {
    sendPackInform(pack, sender);

    if (pack.isNeighbors()) {
        return;  // Do not redirect packs
    }

    if (pack.isFragmented() && pack.getFragmentsNum() > Packet::SmartRedirectTreshold) {
        nh_.redirectByNeighbours(&pack);
    }
    else {
        nh_.neighbourHasPacket(sender, pack.getHash(), false);
        sendBroadcast(&pack);
    }
}

void Transport::sendPackInform(const Packet& pack, RemoteNodePtr& sender) {
    ConnectionPtr conn = nh_.getConnection(sender);
    if (!conn) {
        return;
    }
    sendPackInform(pack, **conn);
}

void Transport::sendPackInform(const Packet& pack, const Connection& addr) {
    cs::Lock lock(oLock_);
    oPackStream_.init(BaseFlags::NetworkMsg);
    oPackStream_ << NetworkCommand::PackInform << static_cast<cs::Byte>(pack.isNeighbors()) << pack.getHash();
    sendDirect(oPackStream_.getPackets(), addr);
    oPackStream_.clear();
}

bool Transport::gotPackInform(const TaskPtr<IPacMan>&, RemoteNodePtr& sender) {
    uint8_t isDirect = 0;
    cs::Hash hHash;
    iPackStream_ >> isDirect >> hHash;

    if (!iPackStream_.good() || !iPackStream_.end()) {
        return false;
    }

    nh_.neighbourHasPacket(sender, hHash, isDirect);
    return true;
}

void Transport::sendPackRenounce(const cs::Hash& hash, const Connection& addr) {
    cs::Lock lock(oLock_);
    oPackStream_.init(BaseFlags::NetworkMsg);

    oPackStream_ << NetworkCommand::PackRenounce << hash;

    sendDirect(oPackStream_.getPackets(), addr);
    oPackStream_.clear();
}

bool Transport::gotPackRenounce(const TaskPtr<IPacMan>&, RemoteNodePtr& sender) {
    cs::Hash hHash;

    iPackStream_ >> hHash;

    if (!iPackStream_.good() || !iPackStream_.end()) {
        return false;
    }

    nh_.neighbourSentRenounce(sender, hHash);

    return true;
}

void Transport::askForMissingPackages() {
    typename decltype(uncollected_)::const_iterator ptr;
    MessagePtr msg;
    size_t i = 0;

    // magic numbers??
    const uint64_t maxMask = 1ull << 63;

    cs::Lock lock(uLock_);
    ptr = uncollected_.begin();

    while (true) {
        if (i >= uncollected_.size()) {
            break;
        }

        msg = *ptr;
        ++ptr;
        ++i;

        {
            cs::Lock messageLock(msg->pLock_);
            const auto end = msg->packets_ + msg->packetsTotal_;

            uint16_t start = 0;
            uint64_t mask = 0;
            uint64_t req = 0;

            for (auto s = msg->packets_; s != end; ++s) {
                if (!*s) {
                    if (!mask) {
                        mask = 1;
                        start = cs::numeric_cast<uint16_t>(s - msg->packets_);
                    }
                    req |= mask;
                }

                if (mask == maxMask) {
                    requestMissing(msg->headerHash_, start, req);

                    if (s > (msg->packets_ + msg->maxFragment_) && (end - s) > 128) {
                        break;
                    }

                    mask = 0;
                }
                else {
                    mask <<= 1;
                }
            }

            if (mask) {
                requestMissing(msg->headerHash_, start, req);
            }
        }
    }
}

void Transport::requestMissing(const cs::Hash& hash, const uint16_t start, const uint64_t req) {
    Packet p;

    {
        cs::Lock lock(oLock_);
        oPackStream_.init(BaseFlags::NetworkMsg);
        oPackStream_ << NetworkCommand::PackRequest << hash << start << req;
        p = *(oPackStream_.getPackets());
        oPackStream_.clear();
    }

    ConnectionPtr requestee = nh_.getNextRequestee(hash);

    if (requestee) {
        sendDirect(&p, **requestee);
    }
}

void Transport::registerMessage(MessagePtr msg) {
    cs::Lock lock(uLock_);
    auto& ptr = uncollected_.emplace(msg);
    //DEBUG:
    Message& message = *ptr.get();
    size_t cnt = message.clearUnused();
    if (cnt > 0) {
        csdebug() << "Net: potential heap corruption detected in uncollected message fragments (" << cnt << ")";
        Transport::cntDirtyAllocs += cnt;
    }
}

bool Transport::gotPackRequest(const TaskPtr<IPacMan>&, RemoteNodePtr& sender) {
    ConnectionPtr conn = nh_.getConnection(sender);
    if (!conn) {
        return false;
    }

    auto ep = conn->specialOut ? conn->out : conn->in;

    cs::Hash hHash;
    uint16_t start = 0u;
    uint64_t req = 0u;

    iPackStream_ >> hHash >> start >> req;

    if (!iPackStream_.good() || !iPackStream_.end()) {
        return false;
    }

    uint32_t reqd = 0, snt = 0;
    uint64_t mask = 1;

    while (mask) {
        if (mask & req) {
            ++reqd;
            if (net_->resendFragment(hHash, start, ep)) {
                ++snt;
            }
        }

        ++start;
        mask <<= 1;
    }

    return true;
}

void Transport::sendPingPack(const Connection& conn) {
    cs::Sequence seq = node_->getBlockChain().getLastSequence();
    cs::Lock lock(oLock_);
    oPackStream_.init(BaseFlags::NetworkMsg);
    oPackStream_ << NetworkCommand::Ping << conn.id << seq << myPublicKey_;

#if defined(PING_WITH_BCHID)
        oPackStream_ << node_->getBlockChain().uuid();
#endif

    sendDirect(oPackStream_.getPackets(), conn);
    oPackStream_.clear();
}

bool Transport::gotPing(const TaskPtr<IPacMan>& task, RemoteNodePtr& sender) {
    Connection::Id id = 0u;
    cs::Sequence lastSeq = 0u;

    cs::PublicKey pk;
    iPackStream_ >> id >> lastSeq >> pk;

#if defined(PING_WITH_BCHID)
    uint64_t remote_bch_uuid = 0;
        iPackStream_ >> remote_bch_uuid;
        uint64_t local_bch_uuid = node_->getBlockChain().uuid();
        if (local_bch_uuid != 0 && remote_bch_uuid != 0) {
            if (local_bch_uuid != remote_bch_uuid) {
                // remote is incompatible
                return false;
            }
        }
#endif

    if (!iPackStream_.good() || !iPackStream_.end()) {
        return false;
    }


    if (lastSeq > maxBlock_) {
        maxBlock_ = lastSeq;
        maxBlockCount_ = 1;
    }

    if (nh_.validateConnectionId(sender, id, task->sender, pk, lastSeq)) {
        emit pingReceived(lastSeq, pk);
    }

    return true;
}
