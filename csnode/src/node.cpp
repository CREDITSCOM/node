#include <algorithm>
#include <csignal>
#include <numeric>
#include <random>
#include <sstream>
#include <numeric>
#include <stdexcept>

#include <solver/consensus.hpp>
#include <solver/solvercore.hpp>
#include <solver/smartcontracts.hpp>

#include <csnode/conveyer.hpp>
#include <csnode/datastream.hpp>
#include <csnode/node.hpp>
#include <csnode/nodecore.hpp>
#include <csnode/nodeutils.hpp>
#include <csnode/poolsynchronizer.hpp>
#include <csnode/itervalidator.hpp>
#include <csnode/blockvalidator.hpp>
#include <csnode/roundpackage.hpp>
#include <csnode/configholder.hpp>
#include <csnode/eventreport.hpp>

#include <lib/system/logger.hpp>
#include <lib/system/progressbar.hpp>
#include <lib/system/signals.hpp>
#include <lib/system/utils.hpp>

#include <net/transport.hpp>
#include <net/packetvalidator.hpp>

#include <base58.h>

#include <boost/optional.hpp>

#include <lz4.h>

#include <cscrypto/cscrypto.hpp>

#include <observer.hpp>
#include <numeric>

namespace {
template<class... Args>
Packet formPacket(BaseFlags flags, MsgTypes msgType, cs::RoundNumber round, Args&&... args) {
    cs::Bytes packetBytes;
    cs::ODataStream stream(packetBytes);
    stream << flags;
    stream << msgType;
    stream << round;
    (void)(stream << ... << std::forward<Args>(args));
    return Packet(std::move(packetBytes));
}
} // namespace

const csdb::Address Node::genesisAddress_ = csdb::Address::from_string("0000000000000000000000000000000000000000000000000000000000000001");
const csdb::Address Node::startAddress_ = csdb::Address::from_string("0000000000000000000000000000000000000000000000000000000000000002");

Node::Node(cs::config::Observer& observer)
: nodeIdKey_(cs::ConfigHolder::instance().config()->getMyPublicKey())
, nodeIdPrivate_(cs::ConfigHolder::instance().config()->getMyPrivateKey())
, blockChain_(genesisAddress_, startAddress_, cs::ConfigHolder::instance().config()->recreateIndex())
, stat_(&blockChain_)
, blockValidator_(std::make_unique<cs::BlockValidator>(*this))
, observer_(observer)
, status_(cs::NodeStatus::ReadingBlocks){
    solver_ = new cs::SolverCore(this, genesisAddress_, startAddress_);

    std::cout << "Start transport... ";
    transport_ = new Transport(this);
    std::cout << "Done\n";
    poolSynchronizer_ = new cs::PoolSynchronizer(&blockChain_);

    cs::ExecutorSettings::set(cs::makeReference(blockChain_), cs::makeReference(solver_));
    auto& executor = cs::Executor::instance();

    cs::Connector::connect(&Node::stopRequested, this, &Node::onStopRequested);

    if (isStopRequested()) {
        stop();
        return;
    }

    // it should work prior WalletsIds & WalletsCache on reading DB
    // to prevent slow BCh reading skip deep validation of already validated blocks
    //cs::Connector::connect(&blockChain_.readBlockEvent(), this, &Node::deepBlockValidation);
    // let blockChain_ to subscribe on signals, WalletsIds & WalletsCache are there
    blockChain_.subscribeToSignals();
    // solver MUST subscribe to signals after the BlockChain
    solver_->subscribeToSignals();
    // continue with subscriptions
    cs::Connector::connect(&blockChain_.readBlockEvent(), &stat_, &cs::RoundStat::onReadBlock);
    cs::Connector::connect(&blockChain_.stopReadingBlocksEvent, &stat_, &cs::RoundStat::onStopReadingFromDb);
    cs::Connector::connect(&blockChain_.readBlockEvent(), this, &Node::validateBlock);
    cs::Connector::connect(&blockChain_.readBlockEvent(), &executor, &cs::Executor::onReadBlock);
    cs::Connector::connect(&blockChain_.storeBlockEvent, &stat_, &cs::RoundStat::onStoreBlock);
    cs::Connector::connect(&blockChain_.storeBlockEvent, &executor, &cs::Executor::onBlockStored);

    cs::Connector::connect(&transport_->neighbourAdded, this, &Node::onNeighbourAdded);
    cs::Connector::connect(&transport_->neighbourRemoved, this, &Node::onNeighbourRemoved);
    cs::Connector::connect(&transport_->pingReceived, &stat_, &cs::RoundStat::onPingReceived);
    cs::Connector::connect(&transport_->mainThreadIterated, &stat_, &cs::RoundStat::onMainThreadIterated);

    cs::Connector::connect(&blockChain_.alarmBadBlock, this, &Node::sendBlockAlarmSignal);
    cs::Connector::connect(&blockChain_.tryToStoreBlockEvent, this, &Node::deepBlockValidation);
    cs::Connector::connect(&blockChain_.stopNode, this, &Node::stop);
    cs::Connector::connect(&blockChain_.storeBlockEvent, this, &Node::processSpecialInfo);
    cs::Connector::connect(&blockChain_.uncertainBlock, this, &Node::sendBlockRequestToConfidants);
    cs::Connector::connect(&blockChain_.orderNecessaryBlock, this, &Node::sendNecessaryBlockRequest);
    cs::Connector::connect(&stat_.accountInitiationRequest, this, &Node::accountInitiationRequest);
    cs::Connector::connect(&blockChain_.successfullQuickStartEvent, this, &Node::onSuccessQS);
    initPoolSynchronizer();
    setupNextMessageBehaviour();
    setupPoolSynchronizerBehaviour();

    good_ = init();
}

Node::~Node() {
    std::cout << "Destructor called\n";

    sendingTimer_.stop();

    delete solver_;
    delete transport_;
    delete poolSynchronizer_;
}

void Node::dumpKnownPeersToFile() {
    std::vector<cs::PeerData> peers;
    transport_->getKnownPeers(peers);
    cs::ConfigHolder::instance().config()->updateKnownHosts(peers);
}

bool Node::init() {
    auto& initConfidants = cs::ConfigHolder::instance().config()->getInitialConfidants();
    initialConfidants_ = decltype(initialConfidants_)(initConfidants.begin(), initConfidants.end());
    if (initialConfidants_.find(solver_->getPublicKey()) != initialConfidants_.end()) {
        transport_->setPermanentNeighbours(initialConfidants_);
    }

#ifdef NODE_API
    std::cout << "Init API... ";

    api_ = std::make_unique<csconnector::connector>(*this, cachesSerializationManager_);

    std::cout << "Done\n";

    cs::Connector::connect(&blockChain_.readBlockEvent(), api_.get(), &csconnector::connector::onReadFromDB);
    cs::Connector::connect(&blockChain_.storeBlockEvent, api_.get(), &csconnector::connector::onStoreBlock);
    cs::Connector::connect(&blockChain_.startReadingBlocksEvent(), api_.get(), &csconnector::connector::onMaxBlocksCount);
    cs::Connector::connect(&cs::Conveyer::instance().packetExpired, api_.get(), &csconnector::connector::onPacketExpired);
    cs::Connector::connect(&cs::Conveyer::instance().transactionsRejected, api_.get(), &csconnector::connector::onTransactionsRejected);

#endif  // NODE_API

    // must call prior to blockChain_.init():
    solver_->init(nodeIdKey_, nodeIdPrivate_, cachesSerializationManager_);
    solver_->startDefault();
    cachesSerializationManager_.bind(stat_);

    if (cs::ConfigHolder::instance().config()->newBlockchainTop()) {
        if (!blockChain_.init(
                cs::ConfigHolder::instance().config()->getPathToDB(),
                &cachesSerializationManager_,
                initialConfidants_,
                cs::ConfigHolder::instance().config()->newBlockchainTopSeq())
        ) {
            csinfo() << "Remove data for QUICK START";
            cachesSerializationManager_.clear();

            return false;
        }
        Consensus::TimeMinStage1 = blockChain_.getTimeMinStage1();
        Consensus::stakingOn = blockChain_.getStakingOn();
        Consensus::miningOn = blockChain_.getMiningOn();
        Consensus::blockReward = blockChain_.getBlockReward();
        Consensus::miningCoefficient = blockChain_.getMiningCoefficient();

        std::string miningStr = Consensus::miningOn ? "true" : "false";
        std::string stakingStr = Consensus::stakingOn ? "true" : "false";
        std::string curMsg = "Changing consensus settings to:\nstakingOn = " + stakingStr
            + "\nminingOn = " + miningStr
            + "\nblockReward = " + Consensus::blockReward.to_string()
            + "\nminingCoefficient = " + Consensus::miningCoefficient.to_string();
        csinfo() << curMsg;
        return true;
    }
    if (!blockChain_.init(
            cs::ConfigHolder::instance().config()->getPathToDB(),
            &cachesSerializationManager_, initialConfidants_)
    ) {
        csinfo() << "Remove data for QUICK START";
        cachesSerializationManager_.clear();

        return false;
    }

    if (initialConfidants_.size() < Consensus::MinTrustedNodes) {
        cslog() << "After reading blockchain, bootstrap nodes number is " << initialConfidants_.size();
    }

    cslog() << "Blockchain is ready, contains " << WithDelimiters(stat_.totalTransactions()) << " transactions";

#ifdef NODE_API
    api_->run();
#endif  // NODE_API

    if (!transport_->isGood()) {
        return false;
    }

    std::cout << "Transport is initialized\n";

    if (!solver_) {
        return false;
    }

    std::cout << "Solver is initialized\n";

    cs::Conveyer::instance().setPrivateKey(solver_->getPrivateKey());
    std::cout << "Initialization finished\n";

    cs::Connector::connect(&transport_->pingReceived, &stat_, &cs::RoundStat::checkPing);
    cs::Connector::connect(&stat_.pingChecked, this, &Node::onPingChecked);

    cs::Connector::connect(&sendingTimer_.timeOut, this, &Node::processTimer);
    cs::Connector::connect(&cs::Conveyer::instance().packetFlushed, this, &Node::onTransactionsPacketFlushed);
    cs::Connector::connect(&poolSynchronizer_->sendRequest, this, &Node::sendBlockRequest);

    
    if (!getBlockChain().getIncorrectBlockNumbers()->empty()) {
        tryResolveHashProblems();
    }
 
    initBootstrapRP(initialConfidants_);
    EventReport::sendRunningStatus(*this, Running::Status::Run);
    globalPublicKey_.fill(0);
    globalPublicKey_.at(31) = 7U;
    return true;
}

void Node::initPoolSynchronizer() {
    cs::Connector::connect(&transport_->pingReceived, poolSynchronizer_, &cs::PoolSynchronizer::onPingReceived);
    cs::Connector::connect(&transport_->neighbourAdded, poolSynchronizer_, &cs::PoolSynchronizer::onNeighbourAdded);
    cs::Connector::connect(&transport_->neighbourRemoved, poolSynchronizer_, &cs::PoolSynchronizer::onNeighbourRemoved);
}

void Node::setupNextMessageBehaviour() {
    cs::Connector::connect(&cs::Conveyer::instance().roundChanged, &stat_, &cs::RoundStat::onRoundChanged);
    cs::Connector::connect(&stat_.roundTimeElapsed, this, &Node::onRoundTimeElapsed);
}

void Node::printInitialConfidants() {
    const cs::PublicKey& own_key = solver_->getPublicKey();
    csinfo() << "Initial confidants: ";
    for (const auto& item : initialConfidants_) {
        const auto beg = item.data();
        const auto end = beg + item.size();
        csinfo() << "NODE> " << " - " << EncodeBase58(beg, end) << (item == own_key ? " (me)" : "");
    }
}

void Node::setupPoolSynchronizerBehaviour() {
    cs::Connector::connect(&blockChain_.storeBlockEvent, &stat_, &cs::RoundStat::onBlockStored);
    cs::Connector::connect(&stat_.storeBlockTimeElapsed, poolSynchronizer_, &cs::PoolSynchronizer::onStoreBlockTimeElapsed);
}

void Node::run() {
    std::cout << "Running transport\n";
    transport_->run();
}

void Node::stop() {
    dumpKnownPeersToFile();
    EventReport::sendRunningStatus(*this, Running::Status::Stop);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // stopping transport stops the node (see Node::run() method)
    transport_->stop();
    cswarning() << "[TRANSPORT STOPPED]";
}

void Node::destroy() {
    good_ = false;

    api_->stop();
    cswarning() << "[API STOPPED]";

    solver_->finish();
    cswarning() << "[SOLVER STOPPED]";

    blockChain_.close();    
    cswarning() << "[BLOCKCHAIN STORAGE CLOSED]";

    observer_.stop();
    cswarning() << "[CONFIG OBSERVER STOPPED]";
}

void Node::initBootstrapRP(const std::set<cs::PublicKey>& confidants) {
    cs::RoundPackage rp;
    cs::RoundTable rt;

    rt.round = getBlockChain().getLastSeq() + 1;

    for (auto& key : confidants) {
        rt.confidants.push_back(key);
    }

    rp.updateRoundTable(rt);
    roundPackageCache_.push_back(rp);
}

void Node::onNeighbourAdded(const cs::PublicKey& neighbour, cs::Sequence lastSeq, cs::RoundNumber lastRound) {
    cslog() << "NODE: new neighbour added " << EncodeBase58(neighbour.data(), neighbour.data() + neighbour.size())
        << " last seq " << lastSeq << " last round " << lastRound;

    if (lastRound > cs::Conveyer::instance().currentRoundNumber()) {
        roundPackRequest(neighbour, lastRound);
        return;
    }
}

void Node::onNeighbourRemoved(const cs::PublicKey& neighbour) {
    cslog() << "NODE: neighbour removed " << EncodeBase58(neighbour.data(), neighbour.data() + neighbour.size());
}

void Node::onSuccessQS(csdb::Amount blockReward, csdb::Amount miningCoeff, bool miningOn, bool stakingOn, uint32_t stageOneHashesTime) {
    Consensus::stakingOn = stakingOn;
    Consensus::miningOn = miningOn;
    Consensus::blockReward = blockReward;
    Consensus::miningCoefficient = miningCoeff;
    Consensus::TimeMinStage1 = stageOneHashesTime;
    std::string msg;
    std::string miningStr = Consensus::miningOn ? "true" : "false";
    std::string stakingStr = Consensus::stakingOn ? "true" : "false";
    std::string curMsg = "Changing consensus settings after qs to:\nstakingOn = " + stakingStr
        + "\nminingOn = " + miningStr
        + "\nblockReward = " + Consensus::blockReward.to_string()
        + "\nminingCoefficient = " + Consensus::miningCoefficient.to_string()
        + "\nTimeMinStage1 = " + std::to_string(Consensus::TimeMinStage1);
    csinfo() << curMsg;
    csinfo() << "Number of contracts: " << solver_->smart_contracts().contracts_count();
    //csinfo() << "Test value = " << solver_->smart_contracts().getTestValue();
}

void Node::getUtilityMessage(const uint8_t* data, const size_t size) {
    cswarning() << "NODE> Utility message get";

    cs::IDataStream stream(data, size);
    cs::Signature sig;
    cs::Bytes msg;
    cs::RoundNumber rNum;
    stream >> msg >> sig ;
    
    if (!stream.isValid() || !stream.isEmpty()) {
        cserror() << "NODE> Bad Utility packet format";
        return;
    }

    cs::Byte order;
    cs::PublicKey pKey;
    cs::IDataStream bytes(msg.data(), msg.size());
    bytes >> rNum;
    bytes >> order;
    bytes >> pKey;

    switch (order) {
        case Orders::Release:
            if (pKey == cs::Zero::key) {
                /* @TODO redesign this code 
                while (transport_->blackList().size() > 0) {
                    addToBlackList(transport_->blackList().front(), false);
                }
                */
            }
            else {
                addToBlackList(pKey, false);
            }

            break;
        case Orders::Seal:
            if (pKey == cs::Zero::key) {
                cswarning() << "Invalid Utility message";
            }
            else {
                addToBlackList(pKey, true);
            }
            break;
        default:
            cswarning() << "Untranslatable Utility message";
            break;
    }
}

uint8_t Node::calculateBootStrapWeight(cs::PublicKeys& confidants) {
    size_t confSize = confidants.size();
    uint8_t currentWeight = 0U;
    if (confSize > 8) {
        return currentWeight;
    }
    //auto it = initialConfidants_.cbegin();
    auto itConfidants = confidants.cbegin();
    --confSize;
    for (auto it : initialConfidants_) {
        if (std::equal(it.cbegin(), it.cend(), itConfidants->cbegin())) {
            currentWeight += 1 << confSize;
            ++itConfidants;
        }
        --confSize;
    }
    return currentWeight;
}


void Node::getBootstrapTable(const uint8_t* data, const size_t size, const cs::RoundNumber rNum) {
    cslog() << "NODE> get Boot strap Round Table #" << rNum;
    solver_->resetGrayList();
    cs::IDataStream in(data, size);
    cs::RoundTable roundTable;
    cs::Bytes payload;
    in >> payload;
    cs::IDataStream stream(payload.data(), payload.size());

    uint8_t confSize = 0;
    stream >> confSize;
    csdebug() << "NODE> Number of confidants :" << cs::numeric_cast<int>(confSize);

    if (confSize < Consensus::MinTrustedNodes || confSize > Consensus::MaxTrustedNodes) {
        cswarning() << "Bad confidants num";
        return;
    }

    cs::ConfidantsKeys confidants;
    confidants.reserve(confSize);

    bool unknown = false;
    for (int i = 0; i < confSize; ++i) {
        cs::PublicKey key;
        stream >> key;
        confidants.push_back(std::move(key));
        if (initialConfidants_.find(key) == initialConfidants_.cend()) {
            unknown = true;
        }
    }

    uint8_t currentWeight = calculateBootStrapWeight(confidants);

    if (unknown) {
        cs::Signature sig;
        stream >> sig;
        if (!stream.isValid()) {
            cswarning() << "malformed bootstrap round table, ignore";
        }
        const size_t signed_subdata_len = 1 + size_t(confSize) * cscrypto::kPublicKeySize;
        const size_t signed_buf_len = sizeof(cs::RoundNumber) + signed_subdata_len;
        cs::Bytes buf;
        buf.reserve(signed_buf_len);
        auto* ptr = reinterpret_cast<const uint8_t*>(&rNum);
        std::copy(ptr, ptr + sizeof(cs::RoundNumber), std::back_inserter(buf));
        ptr = payload.data();
        std::copy(ptr, ptr + signed_subdata_len, std::back_inserter(buf));
        if (!cscrypto::verifySignature(sig, cs::PacketValidator::getBlockChainKey(), buf.data(), buf.size())) {
            cswarning() << kLogPrefix_ << "failed to test bootstrap signature, drop";
            return;
        }
        currentWeight = 255U;
    }
    if (currentWeight <= bootStrapWeight_) {
        return;
    }

    if (!stream.isValid() || confidants.size() < confSize) {
        cswarning() << "Bad round table format, ignore";
        return;
    }
    subRound_ = 1;

    if (!isBootstrapRound_) {
        isBootstrapRound_ = true;
        cslog() << "NODE> Bootstrap on";
    }

    roundTable.confidants = std::move(confidants);
    roundTable.hashes.clear();

    cs::Sequence lastSequence = blockChain_.getLastSeq();

    if (lastSequence >= rNum) {
        csdebug() << "NODE> remove " << lastSequence - rNum + 1 << " block(s) required (rNum = " << rNum << ", last_seq = " << lastSequence << ")";
        blockChain_.setBlocksToBeRemoved(lastSequence - rNum + 1);
    }

    roundTable.round = rNum;
    confirmationList_.remove(rNum);

    cs::Conveyer::instance().updateRoundTable(rNum, roundTable);
    stat_.onRoundChanged();
    onRoundStart(roundTable, false);
    reviewConveyerHashes();

    if (cs::ConfigHolder::instance().config()->isSyncOn()) {
        poolSynchronizer_->sync(rNum);
    }
}

bool Node::verifyPacketSignatures(cs::TransactionsPacket& packet, const cs::PublicKey& sender) {
    std::string verb;
    if (packet.signatures().size() == 1) {
        std::string res = packet.verify(sender);
        if (res.size() > 0) {
            csdebug() << "NODE> Packet " << packet.hash().toString() << " signatures check result: " << res;
            return false;
        }
        else {
            verb = " is Ok";
        }
    }
    else if (packet.signatures().size() > 2) {
        const csdb::Transaction& tr = packet.transactions().front();
        if (cs::SmartContracts::is_new_state(tr)) {
            csdb::UserField fld;
            fld = tr.user_field(cs::trx_uf::new_state::RefStart);
            if (fld.is_valid()) {
                cs::SmartContractRef ref(fld);
                if (ref.is_valid()) {
                    cs::RoundNumber smartRound = ref.sequence;
                    auto block = getBlockChain().loadBlock(smartRound);
                    if (block.is_valid()) {
                        std::string res = packet.verify(block.confidants());
                        if (res.size() > 0) {
                            csdebug() << "NODE> Packet " << packet.hash().toString() << " signatures aren't correct: " << res;
                            return false;
                        }
                        else {
                            verb = "s are Ok";
                        }
                    }
                    else {
                        verb = "s can't be verified. Leave it as is ...";
                    }
                }
                else {
                    csdebug() << "NODE> Contract ref is invalid";
                    return false;
                }
            }
            else {
                csdebug() << "NODE> Contract user fielsd is invalid";
                return false;
            }
        }
        else {
            csdebug() << "NODE> Packet is not possibly correct smart contract packet, throw it";
            return false;
        }
    }
    else {
        cswarning() << "NODE> Usually packets can't have " << packet.signatures().size() << " signatures";
        return false;
    }
    if (verb.size() > 10) {
        csdebug() << "NODE> Packet " << packet.hash().toString() << " signature" << verb;
    }
    return true;
}

void Node::addToBlackListCounter(const cs::PublicKey& key) {
    if (blackListCounter_.find(key) == blackListCounter_.end()) {
        blackListCounter_.emplace(key, Consensus::BlackListCounterSinglePenalty);
    }
    else {
        blackListCounter_.at(key) += Consensus::BlackListCounterSinglePenalty;
        if (blackListCounter_.at(key) > Consensus::BlackListCounterMaxValue) {
            addToBlackList(key, true);
            return;
        }
    }
    csdebug() << "NODE> Node with Key: " << cs::Utils::byteStreamToHex(key.data(), key.size()) << " gained " 
        << Consensus::BlackListCounterSinglePenalty << " penalty points, total: " << blackListCounter_.at(key);
}

void Node::updateBlackListCounter() {
    csdebug() << __func__;
    auto it = blackListCounter_.begin();
    while (it != blackListCounter_.end()) {
        --it->second;
        if (it->second == 0) {
            it = blackListCounter_.erase(it);
            //TODO: make possible to uncomment this code
            //if (isBlackListed(it->first) {
            //    transport_->revertBan(it->first);
            //}
        }
        else {
            ++it;
        }
    }
}

bool Node::verifyPacketTransactions(cs::TransactionsPacket packet, const cs::PublicKey& key) {
    auto lws = getBlockChain().getLastSeq();
    if (lws + cs::ConfigHolder::instance().config()->conveyerData().maxPacketLifeTime < packet.expiredRound()) {
        csdebug() << "NODE> Packet " << packet.hash().toString() << " was added to HashTable without transaction's check";
        return true;
    }

    size_t sum = 0;
    size_t cnt = packet.transactionsCount();

    if (packet.signatures().size() == 1) {
        auto& transactions = packet.transactions();
        if (transactions.size() > Consensus::MaxPacketTransactions) {
            return false;
        }
        for (auto& it : transactions) {
            if (cs::IterValidator::SimpleValidator::validate(it, getBlockChain(), solver_->smart_contracts())) {
                ++sum;
            }
        }
    }
    else if (packet.signatures().size() > 2) {
        if (packet.transactions().size() > Consensus::MaxContractResultTransactions) {
            csdebug() << "NODE> Illegal number of transactions in single packet: " << packet.transactions().size();
            return false;
        }
        return true;
    }
    else {
        csdebug() << "NODE> Illegal number of signatures";
        return false;
    }

    if (sum == cnt) {
        return true;
    }
    else {

        if (cnt > Consensus::AccusalPacketSize) {
            if (sum < cnt/2) {
                // put sender to black list
                csdebug() << "NODE> Sender should be put to the black list: valid trxs = " << sum << ", total trxs = " << cnt;
            }
            else {
                // put sender to black list counter
                csdebug() << "NODE> Sender should be send to the black list counter: valid trxs = " << sum << ", total trxs = " << cnt;
                addToBlackListCounter(key);
                return true;
            }
        }
        else {
            if (sum < cnt / 2) {
                // put sender to black list counter
                csdebug() << "NODE> Sender should be send to the black list counter: valid trxs = " << sum << ", total trxs = " << cnt;
                addToBlackListCounter(key);
                return true;
            }
            else {
                // put sender to black list counter
                csdebug() << "NODE> Sender shouldn't be send to the black list counter: valid trxs = " << sum << ", total trxs = " << cnt;
                return true;
            }
        }

        return false;
    }
}

void Node::getTransactionsPacket(const uint8_t* data, const std::size_t size, const cs::PublicKey& sender) {
    cs::IDataStream stream(data, size);

    cs::TransactionsPacket packet;
    stream >> packet;

    if (packet.hash().isEmpty()) {
        cswarning() << "Received transaction packet hash is empty";
        return;
    }
    
    if (verifyPacketSignatures(packet, sender) && verifyPacketTransactions(packet, sender)) {
        processTransactionsPacket(std::move(packet));
    }
    else {
        addToBlackList(sender, true);
    }
    cs::Bytes toHash = packet.toBinary(cs::TransactionsPacket::Serialization::Transactions);
    if (cs::Conveyer::instance().currentRoundNumber() < Consensus::syncroChangeRound) {
        return;
    }
    if (orderedPackets_.find(packet.hash()) == orderedPackets_.end()) {
        cswarning() << "Received transaction packet was not ordered";
    }
    else {
        orderedPackets_.erase(packet.hash());
    }
    if (cs::Conveyer::instance().currentRoundNumber() < Consensus::syncroChangeRound) {
    }
    else {
        sendPacketHash(packet.hash());
    }
    

}

void Node::getNodeStopRequest(const cs::RoundNumber round, const uint8_t* data, const std::size_t size) {
    const auto localRound = cs::Conveyer::instance().currentRoundNumber();

    if (round < localRound && localRound - round > cs::MaxRoundDeltaInStopRequest) {
        // ignore too aged command to prevent store & re-use by enemies
        return;
    }

    cs::IDataStream stream(data, size);

    uint16_t version = 0;
    cs::Signature sig;
    stream >> version >> sig;

    if (!stream.isValid() || !stream.isEmpty()) {
        cswarning() << "NODE> Get stop request parsing failed";
        return;
    }

    cs::Bytes message;
    cs::ODataStream roundStream(message);
    roundStream << round << version;

    // packet validator have already tested starter signature
    cswarning() << "NODE> Get stop request, received version " << version << ", received bytes " << size;

    if (NODE_VERSION > version) {
        cswarning() << "NODE> stop request does not cover my version, continue working";
        return;
    }

    cswarning() << "NODE> Get stop request, node will be closed...";
    stopRequested_ = true;

    // unconditional stop
    stop();
}

bool Node::canBeTrusted(bool critical) {
#if defined(MONITOR_NODE) || defined(WEB_WALLET_NODE)
    csunused(critical);
    return false;

#else

    if (stopRequested_) {
        return false;
    }

    if (!critical) {
        if (Consensus::DisableTrustedRequestNextRound) {
            // ignore flag after Bootstrap
            if (myLevel_ == Level::Confidant && subRound_ == 0) {
                return false;
            }
        }
    }

    std::string msg;
    if (!checkNodeVersion(getBlockChain().getLastSeq()+2, msg)) {
        return false;
    }

    if (cs::Conveyer::instance().currentRoundNumber() < Consensus::StartingDPOS) {
        csdebug() << "The DPOS doesn't work unless the roundNumber is less than " << Consensus::StartingDPOS;
        return true;
    }

    BlockChain::WalletData wData;
    BlockChain::WalletId wId;

    if (!getBlockChain().findWalletData(csdb::Address::from_public_key(this->nodeIdKey_), wData, wId)) {
        return false;
    }

    if (wData.balance_ + wData.delegated_ < Consensus::MinStakeValue) {
        return false;
    }

    if (!solver_->smart_contracts().executionAllowed()) {
        return false;
    }

    return true;

#endif
}

void Node::getPacketHashesRequest(const uint8_t* data, const std::size_t size, const cs::RoundNumber round, const cs::PublicKey& sender) {
    cs::IDataStream stream(data, size);

    cs::PacketsHashes hashes;
    stream >> hashes;

    std::string sHashes;
    csdebug() << "NODE> Get request for " << hashes.size() 
        << " packet hashes: " << sHashes 
        << " from " << cs::Utils::byteStreamToHex(sender.data(), sender.size());


    if (hashes.empty()) {
        csmeta(cserror) << "Wrong hashes list requested";
        return;
    }

    processPacketsRequest(std::move(hashes), round, sender);
}

void Node::getPacketHashesReply(const uint8_t* data, const std::size_t size, const cs::RoundNumber round, const cs::PublicKey& sender) {
    if (cs::Conveyer::instance().isSyncCompleted(round)) {
        csdebug() << "NODE> sync packets have already finished in round " << round;
        return;
    }

    cs::IDataStream stream(data, size);

    cs::PacketsVector packets;
    stream >> packets;

    if (packets.empty()) {
        csmeta(cserror) << "Packet hashes reply, bad packets parsing";
        return;
    }

    csdebug() << "NODE> Get reply with " << packets.size() <<  " packet hashes from sender " << cs::Utils::byteStreamToHex(sender);
    processPacketsReply(std::move(packets), round);
}


bool Node::checkCharacteristic(cs::RoundPackage& rPackage) {
    auto& conveyer = cs::Conveyer::instance();
    auto myCharacteristic = conveyer.characteristic(rPackage.poolMetaInfo().sequenceNumber);
    csdebug() << "NODE> Trying to get characteristic from conveyer";
    std::vector<cscrypto::Byte> ownMask;
    //std::vector<cscrypto::Byte>::const_iterator myIt;
    if (myCharacteristic == nullptr || myCharacteristic->mask.size() < rPackage.poolMetaInfo().characteristic.mask.size()) {
        csdebug() << "NODE> Characteristic from conveyer doesn't exist";
        auto data = conveyer.createPacket(rPackage.poolMetaInfo().sequenceNumber);

        if (!data.has_value()) {
            cserror() << "NODE> error while prepare to calculate characteristic, maybe method called before sync completed?";
            return false;
        }
        // bindings
        auto&& [packet, smartPackets] = std::move(data).value();
        csdebug() << "NODE> Packet size: " << packet.transactionsCount() << ", smartPackets: " << smartPackets.size();
        auto myMask = solver_->ownValidation(packet, smartPackets);
        csdebug() << "NODE> Characteristic calculated from the very transactions";
        if (myMask.has_value()) {
            ownMask = myMask.value().mask;
        }
        else {
            ownMask.clear();
        }
        //        myIt = maskOnly.cbegin();
    }
    else {
        ownMask = myCharacteristic->mask;
        //        myIt = maskOnly.cbegin();
    }
    auto otherMask = rPackage.poolMetaInfo().characteristic.mask;
    bool identic = true;
    cs::Bytes checkMask;
    csdetails() << "NODE> Starting comparing characteristics: our: " << cs::Utils::byteStreamToHex(ownMask.data(), ownMask.size())
        << " and received: " << cs::Utils::byteStreamToHex(otherMask.data(), otherMask.size());
    //TODO: this code is to be refactored - may cause some problems
    if (otherMask.size() != ownMask.size()) {
        csdebug() << "NODE> masks have different length's";
        identic = false;
    }
    if (identic) {
        for (size_t i = 0; i < ownMask.size(); ++i) {
            if (otherMask[i] != ownMask[i]) {
                identic = false;
                checkMask.push_back(1);
                //csdebug() << "NODE> Comparing own value " << static_cast<int>(ownMask[i]) << " versus " << static_cast<int>(otherMask[i]) << " ... False";
                break;
            }
            else {
                checkMask.push_back(0);
                //csdebug() << "NODE> Comparing own value " << static_cast<int>(ownMask[i]) << " versus " << static_cast<int>(otherMask[i]) << " ... Ok";
            }
        }
    }

    if (!identic) {
        std::string badChecks;
        int badChecksCounter = 0;
        for (size_t i = 0; i < checkMask.size(); ++i) {
            if (checkMask[i] != 0) {
                if (badChecks.size() > 0) {
                    badChecks += ", ";
                }
                badChecks += std::to_string(i);
                ++badChecksCounter;
            }
        }
        cserror() << "NODE> We probably got the roundPackage with invalid characteristic. " << badChecksCounter
            << "; transaction(s): " << badChecks << " (is)were not checked properly. Can't build block";

        cs::PublicKey source_node;
        if (!rPackage.getSender(source_node)) {
            std::copy(cs::Zero::key.cbegin(), cs::Zero::key.cend(), source_node.begin());
        }
        sendBlockAlarm(source_node, rPackage.poolMetaInfo().sequenceNumber);
        return false;
    }
    csdebug() << "NODE> Previous block mask validation finished successfully";
    return true;
}

void Node::getCharacteristic(cs::RoundPackage& rPackage) {
    csmeta(csdetails) << "started";
    cs::Conveyer& conveyer = cs::Conveyer::instance();
    if (getBlockChain().updateLastBlock(rPackage)) {
        csdebug() << "NODE> The last block updated correctly or doesn't need update";
        return;
    }

    auto round = rPackage.poolMetaInfo().sequenceNumber;
    if (!conveyer.isSyncCompleted(rPackage.poolMetaInfo().sequenceNumber)) {
        csdebug() << "NODE> Packet sync not finished, saving characteristic meta to call after sync";

        cs::CharacteristicMeta meta;
        meta.bytes = rPackage.poolMetaInfo().characteristic.mask;
        //meta.sender = sender;
        meta.signatures = rPackage.poolSignatures();
        meta.realTrusted = rPackage.poolMetaInfo().realTrustedMask;

        conveyer.addCharacteristicMeta(round, std::move(meta));
        return;
    }

    csdebug() << "Trying to get confidants from round " << round;
    const auto table = conveyer.roundTable(round);

    if (table == nullptr) {
        cserror() << "NODE> cannot access proper round table to add trusted to pool #" << rPackage.poolMetaInfo().sequenceNumber;
        return;
    }

    const cs::ConfidantsKeys& confidantsReference = table->confidants;
    const std::size_t realTrustedMaskSize = rPackage.poolMetaInfo().realTrustedMask.size();

    csdebug() << "Real TrustedMask size = " << realTrustedMaskSize;

    if (realTrustedMaskSize > confidantsReference.size()) {
        csmeta(cserror) << ", real trusted mask size: " << realTrustedMaskSize << ", confidants count " << confidantsReference.size() << ", on round " << round;
        return;
    }

    csdebug() << "NODE> Sequence " << rPackage.poolMetaInfo().sequenceNumber
        << ", mask size " << rPackage.poolMetaInfo().characteristic.mask.size()
        << ", timestamp " << rPackage.poolMetaInfo().timestamp
        << ", reward " << cs::Utils::byteStreamToHex(rPackage.poolMetaInfo().reward.data(), rPackage.poolMetaInfo().reward.size());

    if (blockChain_.getLastSeq() > rPackage.poolMetaInfo().sequenceNumber) {
        csmeta(cswarning) << "blockChain last seq: " << blockChain_.getLastSeq()
            << " > pool meta info seq: " << rPackage.poolMetaInfo().sequenceNumber;
        return;
    }
    if (rPackage.poolMetaInfo().sequenceNumber > getBlockChain().getLastSeq() && rPackage.poolMetaInfo().sequenceNumber - getBlockChain().getLastSeq() == 1) {
        if (!checkCharacteristic(rPackage)) {
            return;
        }
    }

    // otherwise senseless, this block is already in chain
    conveyer.setCharacteristic(rPackage.poolMetaInfo().characteristic, rPackage.poolMetaInfo().sequenceNumber);
    std::optional<csdb::Pool> pool = conveyer.applyCharacteristic(rPackage.poolMetaInfo());

    if (!pool.has_value()) {
        csmeta(cserror) << "Created pool is not valid";
        return;
    }

//    solver_->uploadNewStates(conveyer.uploadNewStates());

    auto tmp = rPackage.poolSignatures();
    pool.value().set_signatures(tmp);
    pool.value().set_confidants(confidantsReference);
    auto tmpPool = solver_->getDeferredBlock().clone();
    if (tmpPool.is_valid() && tmpPool.sequence() == round) {
        auto tmp2 = rPackage.poolSignatures();
        BlockChain::setTimestamp(tmpPool, rPackage.poolMetaInfo().timestamp);
        if (rPackage.poolMetaInfo().reward.size() > 0) {
            tmpPool.add_user_field(BlockChain::kFieldBlockReward, rPackage.poolMetaInfo().reward);
        }
        if (isBootstrapRound()) {
            BlockChain::setBootstrap(tmpPool, true);
        }
        tmpPool.add_number_trusted(static_cast<uint8_t>(rPackage.poolMetaInfo().realTrustedMask.size()));
        tmpPool.add_real_trusted(cs::Utils::maskToBits(rPackage.poolMetaInfo().realTrustedMask));
        tmpPool.set_signatures(tmp2);
        csdebug() << "Signatures " << tmp2.size() << " were added to the pool: " << tmpPool.signatures().size();
        //tmpPool.add_user_field(BlockChain::kFieldBlockReward, rPackage.poolMetaInfo().reward);
        //csdebug() << "Reward " << rPackage.poolMetaInfo().reward.size() << " were added to the pool: " << cs::Utils::byteStreamToHex(rPackage.poolMetaInfo().reward.data(), rPackage.poolMetaInfo().reward.size());
        auto resPool = getBlockChain().createBlock(tmpPool);

        if (resPool.has_value()) {
            csdebug() << "(From getCharacteristic): " << "The stored properly";
            return;
        }
        else {
            cserror() << "(From getCharacteristic): " << "Blockchain failed to write new block, it will do it later when get proper data";
        }

    }
    if (round != 0) {
        auto confirmation = confirmationList_.find(round);
        if (confirmation.has_value()) {
            if (rPackage.poolMetaInfo().sequenceNumber > 1) {
                pool.value().add_number_confirmations(static_cast<uint8_t>(confirmation.value().mask.size()));
                pool.value().add_confirmation_mask(cs::Utils::maskToBits(confirmation.value().mask));
                pool.value().add_round_confirmations(confirmation.value().signatures);
            }
        }
    }
    if (isBootstrapRound()) {
        BlockChain::setBootstrap(pool.value(), true);
    }
    //blockChain_.addNewWalletsToPool(pool.value());
    if (!blockChain_.storeBlock(pool.value(), cs::PoolStoreType::Created)) {
        cserror() << "NODE> failed to store block in BlockChain";
    }
    else {
        blockChain_.testCachedBlocks();
        solver_->checkZeroSmartSignatures(pool.value());
        //confirmationList_.remove(round);
    }

    csmeta(csdetails) << "done";
}

void Node::sendBlockAlarmSignal(cs::Sequence seq) {
    sendBlockAlarm(cs::Zero::key, seq);
}

void Node::sendBlockAlarm(const cs::PublicKey& source_node, cs::Sequence seq) {
    //cs::Bytes message;
    //cs::DataStream stream(message);
    //stream << seq;
    //cs::Signature sig = cscrypto::generateSignature(solver_->getPrivateKey(), message.data(), message.size());
    //sendToBroadcast(MsgTypes::BlockAlarm, seq, sig);
    //csmeta(csdebug) << "Alarm of block #" << seq << " was successfully sent to all";
    // send event report
    EventReport::sendInvalidBlockAlarm(*this, source_node, seq);
}

void Node::getBlockAlarm(const uint8_t* data, const std::size_t size, const cs::RoundNumber rNum, const cs::PublicKey& sender) {
    cs::IDataStream stream(data, size);

    cs::Signature sig;
    stream >> sig;

    cs::Bytes message;
    cs::ODataStream bytes(message);
    bytes << rNum;

    if (!cscrypto::verifySignature(sig, sender, message.data(), message.size())) {
        csdebug() << "NODE> BlockAlarm message from " << cs::Utils::byteStreamToHex(sender.data(), sender.size()) << " -  WRONG SIGNATURE!!!";
        return;
    }
    else {
        csdebug() << "NODE> Got BlockAlarm message from " << cs::Utils::byteStreamToHex(sender.data(), sender.size());
    }
}

void Node::reportEvent(const cs::Bytes& bin_pack) {
    const auto& conf = cs::ConfigHolder::instance().config()->getEventsReportData();
    if (!conf.is_active) {
        return;
    }
    cs::Bytes message;
    cs::ODataStream stream(message);

    constexpr uint8_t kEventReportVersion = 0;

    if constexpr (kEventReportVersion == 0) {
        stream << kEventReportVersion << blockChain_.getLastSeq() << bin_pack;
    }
    cs::Signature sig = cscrypto::generateSignature(solver_->getPrivateKey(), message.data(), message.size());
    
    cs::PublicKey receiver;
    {
        cs::Bytes publicKey;
        if (!DecodeBase58(conf.collector_id, publicKey)) {
            cserror() << "Wrong events collector id in config, unable to send report";
            return;
        }
        std::copy(publicKey.begin(), publicKey.end(), receiver.begin());
    }

    transport_->sendDirect(formPacket(BaseFlags::Signed, MsgTypes::EventReport, cs::Conveyer::instance().currentRoundNumber(), sig, message),
                           receiver);
    csmeta(csdebug) << "event report (id=" << uint32_t(EventReport::getId(bin_pack)) << ") -> " << conf.collector_id;
}

void Node::getEventReport(const uint8_t* data, const std::size_t size, const cs::RoundNumber rNum, const cs::PublicKey& sender) {
    cs::IDataStream stream(data, size);

    cs::Signature sig;
    cs::Bytes message;
    stream >> sig >> message;

    if (!cscrypto::verifySignature(sig, sender, message.data(), message.size())) {
        csdebug() << "NODE> event report from " << cs::Utils::byteStreamToHex(sender.data(), sender.size()) << " -  WRONG SIGNATURE!!!";
        return;
    }

    cs::IDataStream bytes(message.data(), message.size());
    uint8_t report_version = 0;
    cs::Sequence sender_last_block = 0;
    cs::Bytes bin_pack;
    bytes >> report_version;
    if (report_version == 0) {
        stream >> sender_last_block >> bin_pack;
        csdebug() << "NODE> Got event report from " << cs::Utils::byteStreamToHex(sender.data(), sender.size())
            << ", sender round R-" << WithDelimiters(rNum)
            << ", sender last block #" << WithDelimiters(sender_last_block)
            << ", info size " << bin_pack.size();

        std::ostringstream os;
        os << "Event report: " << '[' << WithDelimiters(rNum) << "] ";
        std::string log_prefix = os.str();

        const auto event_id = EventReport::getId(bin_pack);
        if (event_id == EventReport::Id::RejectTransactions) {
            const auto resume = EventReport::parseReject(bin_pack);
            if (!resume.empty()) {
                size_t cnt = 0;
                std::ostringstream os_rej;
                std::for_each(resume.cbegin(), resume.cend(), [&](const auto& item) {
                    cnt += item.second;
                    os_rej << Reject::to_string(item.first) << " (" << item.second << ") ";
                    });
                csevent() << log_prefix << "rejected " << cnt << " transactions the following reasons: " << os_rej.str()
                    << " on " << cs::Utils::byteStreamToHex(sender.data(), sender.size());
            }
        }
        else if (event_id == EventReport::Id::AddToList || event_id == EventReport::Id::EraseFromList) {
            bool added = event_id == EventReport::Id::AddToList;
            std::string list_action = (added ? "added to" : "cleared from");
            cs::PublicKey item;
            uint32_t counter = std::numeric_limits<uint32_t>::max();
            bool is_black = false;
            if (EventReport::parseListUpdate(bin_pack, item, counter, is_black)) {
                std::string list_name = (is_black ? "black" : "gray");
                if (std::equal(item.cbegin(), item.cend(), cs::Zero::key.cbegin())) {
                    csevent() << log_prefix << "all items are " << list_action << ' ' << list_name << " list on "
                        << cs::Utils::byteStreamToHex(sender.data(), sender.size());
                }
                else {
                    csevent() << log_prefix << cs::Utils::byteStreamToHex(item.data(), item.size())
                        << ' ' << list_action << ' ' << list_name << " list on "
                        << cs::Utils::byteStreamToHex(sender.data(), sender.size());
                }
            }
            else {
                csevent() << log_prefix << "failed to parse item " << list_action << " black list from "
                    << cs::Utils::byteStreamToHex(sender.data(), sender.size());
            }
        }
        else if (event_id == EventReport::Id::AlarmInvalidBlock) {
            cs::PublicKey source_node;
            cs::Sequence invalid_block_seq;
            if (EventReport::parseInvalidBlockAlarm(bin_pack, source_node, invalid_block_seq)) {
                csevent() << log_prefix << "invalid block from "
                    << cs::Utils::byteStreamToHex(source_node.data(), source_node.size())
                    << " is alarmed by " << cs::Utils::byteStreamToHex(sender.data(), sender.size());
            }
            else {
                csevent() << log_prefix << "failed to parse invalid block alarm report from "
                    << cs::Utils::byteStreamToHex(sender.data(), sender.size());
            }
        }
        else if (event_id == EventReport::Id::ConsensusSilent || event_id == EventReport::Id::ConsensusLiar) {
            cs::PublicKey problem_node;
            std::string problem_name = (event_id == EventReport::ConsensusSilent ? "silent" : "liar");
            if (EventReport::parseConsensusProblem(bin_pack, problem_node) != EventReport::Id::None) {
                csevent() << log_prefix << cs::Utils::byteStreamToHex(problem_node.data(), problem_node.size())
                    << " is reported as " << problem_name << " by " << cs::Utils::byteStreamToHex(sender.data(), sender.size())
                    << " in round consensus";
            }
            else {
                csevent() << log_prefix << "failed to parse invalid round consensus " << problem_name << " report from "
                    << cs::Utils::byteStreamToHex(sender.data(), sender.size());
            }
        }
        else if (event_id == EventReport::Id::ConsensusFailed) {
            csevent() << log_prefix << "round consensus failure is reported by " << cs::Utils::byteStreamToHex(sender.data(), sender.size());
        }
        else if (event_id == EventReport::Id::ContractsSilent || event_id == EventReport::Id::ContractsLiar || event_id == EventReport::Id::ContractsFailed) {
            cs::PublicKey problem_node;
            ContractConsensusId consensus_id;
            std::string problem_name;
            if (event_id == EventReport::ContractsSilent) {
                problem_name = "silent";
            }
            else if (event_id == EventReport::ContractsSilent) {
                problem_name = "liar";
            }
            else {
                problem_name = "failure";
            }
            if (EventReport::parseContractsProblem(bin_pack, problem_node, consensus_id) != EventReport::Id::None) {
                if (event_id != EventReport::Id::ContractsFailed) {
                    csevent() << log_prefix << cs::Utils::byteStreamToHex(problem_node.data(), problem_node.size())
                        << " is reported as " << problem_name << " by " << cs::Utils::byteStreamToHex(sender.data(), sender.size())
                        << " in contract consensus {" << consensus_id.round << '.' << consensus_id.transaction << '.' << consensus_id.iteration << '}';
                }
                else {
                    csevent() << log_prefix << cs::Utils::byteStreamToHex(problem_node.data(), problem_node.size())
                        << " is reported failure in contract consensus {"
                        << consensus_id.round << '.' << consensus_id.transaction << '.' << consensus_id.iteration << '}';
                }
            }
            else {
                csevent() << log_prefix << "failed to parse invalid contract consensus " << problem_name << " report from "
                    << cs::Utils::byteStreamToHex(sender.data(), sender.size());
            }
        }
        else if (event_id == EventReport::Id::RejectContractExecution) {
            cs::SmartContractRef ref;
            Reject::Reason reason;
            if (EventReport::parseRejectContractExecution(bin_pack, ref, reason)) {
                csevent() << log_prefix << "execution of " << ref << " is rejected by "
                    << cs::Utils::byteStreamToHex(sender.data(), sender.size()) << ", " << Reject::to_string(reason);
            }
            else {
                csevent() << log_prefix << "failed to parse invalid contract execution reject from "
                    << cs::Utils::byteStreamToHex(sender.data(), sender.size());
            }
        }
        else if (event_id == EventReport::Id::Bootstrap) {
            csevent() << log_prefix << "bootsrap round on " << cs::Utils::byteStreamToHex(sender.data(), sender.size());
        }
        else if (event_id == EventReport::RunningStatus) {
            Running::Status status;
            if (EventReport::parseRunningStatus(bin_pack, status)) {
                csevent() << log_prefix << cs::Utils::byteStreamToHex(sender.data(), sender.size())
                    << " go to state " << Running::to_string(status);
                stat_.setNodeStatus(sender, status == Running::Status::Stop);
            }
            else {
                csevent() << log_prefix << "failed to parse invalid running status from "
                    << cs::Utils::byteStreamToHex(sender.data(), sender.size());
            }
        }
    }
    else {
        csevent() << "NODE> Got event report from " << cs::Utils::byteStreamToHex(sender.data(), sender.size())
            << " of incompatible version " << int(report_version)
            << ", sender round R-" << WithDelimiters(rNum);
    }
}

void Node::cleanConfirmationList(cs::RoundNumber rNum) {
    confirmationList_.remove(rNum);
}

void Node::sendStateRequest(const csdb::Address& contract_abs_addr, const cs::PublicKeys& confidants) {
    csmeta(csdebug) << cs::SmartContracts::to_base58(blockChain_, contract_abs_addr);

    auto round = cs::Conveyer::instance().currentRoundNumber();
    cs::Bytes message;
    cs::ODataStream stream(message);
    const auto& key = contract_abs_addr.public_key();
    stream << round << key;
    cs::Signature sig = cscrypto::generateSignature(solver_->getPrivateKey(), message.data(), message.size());

    sendDirect(confidants, MsgTypes::StateRequest, round, key, sig);
}

void Node::getStateRequest(const uint8_t * data, const std::size_t size, const cs::RoundNumber rNum, const cs::PublicKey & sender) {
    cs::IDataStream stream(data, size);

    cs::PublicKey key;
    cs::Signature signature;
    stream >> key >> signature;

    csdb::Address abs_addr = csdb::Address::from_public_key(key);

    csmeta(csdebug) << cs::SmartContracts::to_base58(blockChain_, abs_addr) << " from "
        << cs::Utils::byteStreamToHex(sender.data(), sender.size());

    if (!stream.isValid() || !stream.isEmpty()) {
        cserror() << "NODE> Bad StateRequest packet format";
        return;
    }

    cs::Bytes signed_bytes;
    cs::ODataStream bytesStream(signed_bytes);
    bytesStream << rNum << key;

    if (!cscrypto::verifySignature(signature, sender, signed_bytes.data(), signed_bytes.size())) {
        csdebug() << "NODE> StateRequest Signature is incorrect";
        return;
    }

    cs::Bytes contract_data;

    if (blockChain_.getContractData(abs_addr, contract_data)) {
        sendStateReply(sender, abs_addr, contract_data);
    }
}

void Node::sendStateReply(const cs::PublicKey& respondent, const csdb::Address& contract_abs_addr, const cs::Bytes& data) {
    csmeta(csdebug) << cs::SmartContracts::to_base58(blockChain_, contract_abs_addr)
        << "to " << cs::Utils::byteStreamToHex(respondent.data(), respondent.size());

    cs::RoundNumber round = cs::Conveyer::instance().currentRoundNumber();
    cs::Bytes signed_data;
    cs::ODataStream stream(signed_data);

    const cs::PublicKey& key = contract_abs_addr.public_key();
    stream << round << key << data;

    cs::Signature sig = cscrypto::generateSignature(solver_->getPrivateKey(), signed_data.data(), signed_data.size());
    sendDirect(respondent, MsgTypes::StateReply, round, key, data, sig);
}

void Node::getStateReply(const uint8_t* data, const std::size_t size, const cs::RoundNumber rNum, const cs::PublicKey& sender) {
    cs::IDataStream stream(data, size);

    cs::PublicKey key;
    cs::Bytes contract_data;
    cs::Signature signature;
    stream >> key >> contract_data >> signature;

    csdb::Address abs_addr = csdb::Address::from_public_key(key);
    
    csmeta(csdebug) << cs::SmartContracts::to_base58(blockChain_, abs_addr) << " from "
        << cs::Utils::byteStreamToHex(sender.data(), sender.size());

    if (!stream.isValid() || !stream.isEmpty()) {
        cserror() << "NODE> Bad State packet format";
        return;
    }

    cs::Bytes signed_data;
    cs::ODataStream signed_stream(signed_data);
    signed_stream << rNum << key << contract_data;

    if (!cscrypto::verifySignature(signature, sender, signed_data.data(), signed_data.size())) {
        csdebug() << "NODE> State Signature is incorrect";
        return;
    }

    /*Place here the function call with (state)*/
    solver_->smart_contracts().net_update_contract_state(abs_addr, contract_data);
}

cs::ConfidantsKeys Node::retriveSmartConfidants(const cs::Sequence startSmartRoundNumber) const {
    csmeta(csdebug);

    const cs::RoundTable* table = cs::Conveyer::instance().roundTable(startSmartRoundNumber);
    if (table != nullptr) {
        return table->confidants;
    }

    csdb::Pool tmpPool = blockChain_.loadBlock(startSmartRoundNumber);
    const cs::ConfidantsKeys& confs = tmpPool.confidants();
    csdebug() << "___[" << startSmartRoundNumber << "] = [" << tmpPool.sequence() << "]: " << confs.size();
    return confs;
}

void Node::sendTransactionsPacket(const cs::TransactionsPacket& packet) { //broadcast transaction's packet - deprecated
    if (packet.hash().isEmpty()) {
        cswarning() << "Send transaction packet with empty hash failed";
        return;
    }
    csdebug() << "NODE> Sending transaction's packet with hash: " << cs::Utils::byteStreamToHex(packet.hash().toBinary().data(), packet.hash().size());
    sendBroadcast(MsgTypes::TransactionPacket, cs::Conveyer::instance().currentRoundNumber(), packet);
}

void Node::sendTransactionsPacketHash(const cs::TransactionsPacket& packet) { //instead of packet
    if (packet.hash().isEmpty()) {
        cswarning() << "Send transaction packet with empty hash failed";
        return;
    }
    sendBroadcast(MsgTypes::TransactionPacketHash, cs::Conveyer::instance().currentRoundNumber(), packet.hash());
}

void Node::sendPacketHash(const cs::TransactionsPacketHash& hash) { //
    csdebug() << "NODE> Sending transaction's packet hash (only): " << cs::Utils::byteStreamToHex(hash.toBinary().data(), hash.size());
    sendBroadcast(MsgTypes::TransactionPacketHash, cs::Conveyer::instance().currentRoundNumber(), hash);
}

void Node::getPacketHash(const uint8_t* data, const std::size_t size, const cs::RoundNumber rNum, const cs::PublicKey& sender) {
    if (cs::Conveyer::instance().currentRoundNumber() < Consensus::syncroChangeRound) {
        return;
    }
    cs::IDataStream stream(data, size);
    cs::TransactionsPacketHash pHash;
    stream >> pHash;
    csdebug() << "get packetHash " << pHash.toString() << " from " << cs::Utils::byteStreamToHex(sender);
    const cs::Conveyer& conveyer = cs::Conveyer::instance();
    if (conveyer.isPacketAtMeta(pHash)) {
        csdebug() << "packetHash " << pHash.toString() << " is in meta";
        return;
    }
    if (orderedPackets_.find(pHash) != orderedPackets_.end()) {
        csdebug() << "packetHash " << pHash.toString() << " is ordered";
        return;
    }
    orderedPackets_.emplace(pHash, rNum);
    cs::PacketsHashes pHashes;
    pHashes.push_back(pHash);
    //sendPacketHashesRequest(pHashes, conveyer.currentRoundNumber(), 0);
    csdebug() << "sending packetHashesRequest " << pHash.toString() << " to " << cs::Utils::byteStreamToHex(sender);
    sendPacketHashRequest(pHashes, sender, conveyer.currentRoundNumber());
}

void Node::sendPacketHashRequest(const cs::PacketsHashes& hashes, const cs::PublicKey& respondent, cs::RoundNumber round) {
    if (cs::Conveyer::instance().currentRoundNumber() < Consensus::syncroChangeRound) {
        return;
    }
    sendDirect(respondent, MsgTypes::TransactionsPacketBaseRequest, round, hashes);
}

void Node::getPacketHashRequest(const uint8_t* data, const std::size_t size, const cs::RoundNumber round, const cs::PublicKey& sender) {

    cs::IDataStream stream(data, size);

    cs::PacketsHashes hashes;
    stream >> hashes;
    std::string sHashes;
    for (auto it : hashes) {
        sHashes += it.toString() + ", ";
    }
    csdebug() << "NODE> Get request for " << hashes.size() << " packet hashes: " << sHashes << "from " << cs::Utils::byteStreamToHex(sender.data(), sender.size());

    if (hashes.empty()) {
        csmeta(cserror) << "Wrong hashes list requested";
        return;
    }

    processPacketsBaseRequest(std::move(hashes), round, sender);
}

void Node::processPacketsBaseRequest(cs::PacketsHashes&& hashes, const cs::RoundNumber round, const cs::PublicKey& sender) {
    csdebug() << "NODE> Processing packets base request";

    cs::PacketsVector packets;

    const auto& conveyer = cs::Conveyer::instance();
    std::unique_lock<cs::SharedMutex> lock = conveyer.lock();

    for (const auto& hash : hashes) {
        std::optional<cs::TransactionsPacket> packet = conveyer.findPacket(hash, round);

        if (packet) {
            packets.push_back(std::move(packet).value());
        }
    }

    if (packets.empty()) {
        csdebug() << "NODE> Cannot find packets in storage";
    }
    else {
        csdebug() << "NODE> Found packets in storage: " << packets.size();
        sendPacketHashesBaseReply(packets, round, sender);
    }
}

void Node::sendPacketHashesBaseReply(const cs::PacketsVector& packets, const cs::RoundNumber round, const cs::PublicKey& target) {
    if (packets.empty()) {
        return;
    }

    csdebug() << "NODE> Base reply transaction packets: " << packets.size();
    sendDirect(target, MsgTypes::TransactionsPacketBaseReply, round, packets);
}

void Node::getPacketHashesBaseReply(const uint8_t* data, const std::size_t size, const cs::RoundNumber round, const cs::PublicKey& sender) {
    cs::IDataStream stream(data, size);

    cs::PacketsVector packets;
    stream >> packets;

    if (packets.empty()) {
        csmeta(cserror) << "Packet hashes reply, bad packets parsing";
        return;
    }

    csdebug() << "NODE> Get base reply with " << packets.size() << " packet hashes from sender " << cs::Utils::byteStreamToHex(sender);
    cs::Conveyer& conveyer = cs::Conveyer::instance();
    for (auto&& packet : packets) {
        packet.makeHash();
        if (orderedPackets_.find(packet.hash()) != orderedPackets_.end()) {
            conveyer.addExternalPacketToMeta(std::move(packet));
        }

    }
}


void Node::sendPacketHashesRequest(const cs::PacketsHashes& hashes, const cs::RoundNumber round, uint32_t requestStep) {
    const cs::Conveyer& conveyer = cs::Conveyer::instance();

    if (conveyer.isSyncCompleted(round)) {
        return;
    }

    csdebug() << "NODE> Sending packet hashes request: " << hashes.size();

    if (transport_->getNeighboursCount() == 0) {
        cswarning() << csname() << "Can not send packet hashes to neighbours: no neighbours";
    }
    else {
        transport_->forEachNeighbour([this, round, &hashes](const cs::PublicKey& neighbour, cs::Sequence, cs::RoundNumber) {
                                        sendDirect(neighbour, MsgTypes::TransactionsPacketRequest, round, hashes);
                                     });
    }

    auto requestClosure = [round, requestStep, this] {
        const cs::Conveyer& conveyer = cs::Conveyer::instance();

        if (!conveyer.isSyncCompleted(round)) {
            auto neededHashes = conveyer.neededHashes(round);
            if (neededHashes) {
                sendPacketHashesRequest(*neededHashes, round, requestStep + packetRequestStep_);
            }
        }
    };

    // send request again
    cs::Timer::singleShot(static_cast<int>(cs::NeighboursRequestDelay + requestStep), cs::RunPolicy::CallQueuePolicy, requestClosure);
}


void Node::sendPacketHashesReply(const cs::PacketsVector& packets, const cs::RoundNumber round, const cs::PublicKey& target) {
    if (packets.empty()) {
        return;
    }

    csdebug() << "NODE> Reply transaction packets: " << packets.size();
    sendDirect(target, MsgTypes::TransactionsPacketReply, round, packets);
}

void Node::sendSyncroMessage(cs::Byte msg, const cs::PublicKey& target) {
    csdebug() << __func__;
    if (cs::Conveyer::instance().currentRoundNumber() < Consensus::syncroChangeRound) {
        return;
    }
    sendDirect(target, MsgTypes::SyncroMsg, cs::Conveyer::instance().currentRoundNumber(), msg);
}

void Node::getSyncroMessage(const uint8_t* data, const size_t size, const cs::PublicKey& sender) {
    //csdebug() << __func__;
    if (cs::Conveyer::instance().currentRoundNumber() < Consensus::syncroChangeRound) {
        return;
    }
    cs::Byte msg;
    cs::IDataStream stream(data, size);
    stream >> msg;
    poolSynchronizer_->getSyncroMessage(sender, static_cast<cs::SyncroMessage>(msg));
}

void Node::addSynchroRequestsLog(const cs::PublicKey& sender, cs::Sequence seq, cs::SyncroMessage msg) {
    if (cs::Conveyer::instance().currentRoundNumber() < Consensus::syncroChangeRound) {
        return;
    }
    //csdebug() << __func__;
    synchroRequestsLog_.emplace(sender, std::make_tuple(seq, msg, cs::Utils::currentTimestamp()));
    //csdebug() << __func__ << " -> " << synchroRequestsLog_.size() << ": key " << cs::Utils::byteStreamToHex(sender) << " as " << static_cast<int>(msg) << " at " << std::get<2>(synchroRequestsLog_[sender]);
}

bool Node::changeSynchroRequestsLog(const cs::PublicKey& sender, cs::SyncroMessage msg) {
    if (cs::Conveyer::instance().currentRoundNumber() < Consensus::syncroChangeRound) {
        return true;
    }
    //csdebug() << __func__;
    if (synchroRequestsLog_.find(sender) == synchroRequestsLog_.end()) {
        return false;
    }
    synchroRequestsLog_[sender] = std::make_tuple(std::get<0>(synchroRequestsLog_[sender]), msg, cs::Utils::currentTimestamp());
    //csdebug() << __func__ << " -> " << synchroRequestsLog_.size() << ": key " << cs::Utils::byteStreamToHex(sender) << " changed to " << static_cast<int>(msg) << " at " << std::get<2>(synchroRequestsLog_[sender]);
    return true;
}

void Node::updateSynchroRequestsLog() {
    if (cs::Conveyer::instance().currentRoundNumber() < Consensus::syncroChangeRound) {
        return;
    }
    //csdebug() << __func__ << ": initial size: " << synchroRequestsLog_.size();
    auto timeNow = cs::Utils::currentTimestamp();
    auto it = synchroRequestsLog_.begin();
    bool erasedData = false;
    int cnt = 0;
    while (it != synchroRequestsLog_.end()) {
        if (cnt > 100) {
            csdebug() << "break used to leave dead loop: synchroRequestsLog_.size() = " << synchroRequestsLog_.size();
            break;
        }

        auto msg = std::get<1>(it->second);
        auto timeEvent = std::get<2>(it->second);
        //csdebug() << cs::Utils::byteStreamToHex(it->first) << ": " << std::get<0>(it->second) << ", time: " << timeEvent << ", status: " << static_cast<int>(msg);
        switch (msg) {
        case cs::SyncroMessage::Sent:
            if (timeNow > timeEvent + 5000) {
                csdebug() << __func__ << " -> " << synchroRequestsLog_.size() << ": key " << cs::Utils::byteStreamToHex(it->first) << " removed more than 5s after sent";
                it = synchroRequestsLog_.erase(it);
                erasedData = true;

            }
            break;
        case cs::SyncroMessage::AwaitAnswer:
            break;
            //case cs::SyncroMessage::DuplicatedRequest:
            //case cs::SyncroMessage::IncorrectRequest:
            //case cs::SyncroMessage::NoAnswer:
            //case cs::SyncroMessage::NoSuchBlocks:
        default:
            //csdebug() << __func__ << " -> " << synchroRequestsLog_.size() << ": key " << cs::Utils::byteStreamToHex(it->first) << " removed";
            it = synchroRequestsLog_.erase(it);
            erasedData = true;

        }
        if (!erasedData) {
            ++it;
        }
        ++cnt;
    }
    //csdebug() << __func__ << " -> " << synchroRequestsLog_.size();
}

bool Node::removeSynchroRequestsLog(const cs::PublicKey& sender) {
    if (cs::Conveyer::instance().currentRoundNumber() < Consensus::syncroChangeRound) {
        return true;
    }
    //csdebug() << __func__ << ": " << synchroRequestsLog_.size();
    auto it = synchroRequestsLog_.find(sender);
    if (it == synchroRequestsLog_.end()) {
        //csdebug() << __func__ << ": no such key";
        return false;
    }
    //csdebug() << __func__ << " -> " << synchroRequestsLog_.size() << ": key " << cs::Utils::byteStreamToHex(sender) << " removed";
    it = synchroRequestsLog_.erase(it);
    return true;
}

bool Node::checkSynchroRequestsLog(const cs::PublicKey& sender, cs::Sequence seq) {
    if (cs::Conveyer::instance().currentRoundNumber() < Consensus::syncroChangeRound) {
        return true;
    }
    updateSynchroRequestsLog();
    //csdebug() << __func__ << ": size = " << synchroRequestsLog_.size();
    if (synchroRequestsLog_.find(sender) != synchroRequestsLog_.end()) {
        cs::Sequence maxSeq = 0;
        for (auto it : synchroRequestsLog_) {
            if (it.first != sender) {
                continue;
            }
            auto curSeq = std::get<0>(it.second);
            if (curSeq > maxSeq) {
                maxSeq = curSeq;
            }
        }
        if (maxSeq >= seq) {
            csdebug() << __func__ << ": key " << cs::Utils::byteStreamToHex(sender) << " is false";
            return false;
        }
    }
    csdebug() << __func__ << ": key " << cs::Utils::byteStreamToHex(sender) << " is true";
    return true;
}


void Node::getBlockRequest(const uint8_t* data, const size_t size, const cs::PublicKey& sender) {
    csmeta(csdebug);
    cs::PoolsRequestedSequences sequences;

    cs::IDataStream stream(data, size);
    stream >> sequences;

    if (sequences.empty()) {
        csmeta(cserror) << "Sequences size is 0";
        sendSyncroMessage(cs::SyncroMessage::IncorrectRequest, sender);
        return;
    }

    if (!checkSynchroRequestsLog(sender, sequences.back())) {
        csdebug() << __func__ << ": Dupplicate request";
        sendSyncroMessage(cs::SyncroMessage::DuplicatedRequest, sender);
        return;
    }

    csdebug() << "NODE> got request for " << sequences.size() << " block(s) from " << cs::Utils::byteStreamToHex(sender.data(), sender.size());


    if (sequences.front() > blockChain_.getLastSeq()) {
        csdebug() << "NODE> Get block request> The requested block: " << sequences.front() << " is beyond my last block";
        sendSyncroMessage(cs::SyncroMessage::NoSuchBlocks, sender);
        return;
    }
    addSynchroRequestsLog(sender, sequences.back(), cs::SyncroMessage::AwaitAnswer);
    sendSyncroMessage(cs::SyncroMessage::AwaitAnswer, sender);

    const std::size_t reserveSize = sequences.size();

    cs::PoolsBlock poolsBlock;
    poolsBlock.reserve(reserveSize);

    auto sendReply = [&] {
        sendBlockReply(poolsBlock, sender);
        poolsBlock.clear();
    };

    for (auto& sequence : sequences) {
        csdb::Pool pool = blockChain_.loadBlockForSync(sequence);

        if (pool.is_valid()) {
            poolsBlock.push_back(std::move(pool));
        }
        else {
            csmeta(cslog) << "unable to load block " << sequence << " from blockchain";
        }
    }

    sendReply();
}

void Node::getBlockReply(const uint8_t* data, const size_t size, const cs::PublicKey& sender) {
    csinfo() << __func__ << " from " << cs::Utils::byteStreamToHex(sender);

    bool isSyncOn = poolSynchronizer_->isSyncroStarted();
    bool isBlockchainUncertain = blockChain_.isLastBlockUncertain();

    if (!isSyncOn && !isBlockchainUncertain/* && poolSynchronizer_->getTargetSequence() == 0ULL*/) {
        csdebug() << "NODE> Get block reply> Pool sync has already finished";
        return;
    }

    csdebug() << "NODE> Get Block Reply";

    cs::IDataStream stream(data, size);

    cs::CompressedRegion region;
    stream >> region;

    cs::PoolsBlock poolsBlock = compressor_.decompress<cs::PoolsBlock>(region);

    if (poolsBlock.empty()) {
        cserror() << "NODE> Get block reply> No pools found";
        return;
    }

    if (poolsBlock.size() == 1 && poolsBlock.back().sequence() == neededSequence_) {
        getNecessaryBlockRequest(poolsBlock, sender);
        return;
    }

    if (isBlockchainUncertain) {
        const auto last = blockChain_.getLastSeq();

        for (auto& b: poolsBlock) {
            if (b.sequence() == last) {
                cslog() << kLogPrefix_ << "get possible replacement for uncertain block " << WithDelimiters(last);
                blockChain_.storeBlock(b, cs::PoolStoreType::Synced);
            }
        }
    }

    if (isSyncOn) {
        poolSynchronizer_->getBlockReply(std::move(poolsBlock), sender);
    }
}

void Node::sendBlockReply(const cs::PoolsBlock& poolsBlock, const cs::PublicKey& target) {
    if (poolsBlock.empty()) {
        return;
    }

    for (const auto& pool : poolsBlock) {
        csdetails() << "NODE> Send block reply. Sequence: " << pool.sequence();
    }
    
    csdebug() << "NODE> Sending " << poolsBlock.size() << " blocks with signatures from " << poolsBlock.front().sequence() << " to " << poolsBlock.back().sequence();

    for (const auto& it : poolsBlock) {
        csdetails() << "#" << it.sequence() << " signs = " << it.signatures().size();
    }

    auto region = compressor_.compress(poolsBlock);
    csdebug() << "NODE> block package compressed";
    changeSynchroRequestsLog(target, cs::SyncroMessage::Sent);

    sendDirect(target, MsgTypes::RequestedBlock, cs::Conveyer::instance().currentRoundNumber(), region);
    csdebug() << "NODE> block package sent";
}

void Node::becomeWriter() {
    myLevel_ = Level::Writer;
    csdebug() << "NODE> Became writer";
}

void Node::processPacketsRequest(cs::PacketsHashes&& hashes, const cs::RoundNumber round, const cs::PublicKey& sender) {
    csdebug() << "NODE> Processing packets sync request";

    cs::PacketsVector packets;

    const auto& conveyer = cs::Conveyer::instance();
    std::unique_lock<cs::SharedMutex> lock = conveyer.lock();

    for (const auto& hash : hashes) {
        std::optional<cs::TransactionsPacket> packet = conveyer.findPacket(hash, round);

        if (packet) {
            packets.push_back(std::move(packet).value());
        }
    }

    if (packets.empty()) {
        csdebug() << "NODE> Cannot find packets in storage";
    }
    else {
        csdebug() << "NODE> Found packets in storage: " << packets.size();
        sendPacketHashesReply(packets, round, sender);
    }
}

void Node::processPacketsReply(cs::PacketsVector&& packets, const cs::RoundNumber round) {
    csdebug() << "NODE> Processing packets reply";
    cs::Conveyer& conveyer = cs::Conveyer::instance();

    for (auto&& packet : packets) {
        conveyer.addFoundPacket(round, std::move(packet));
    }

    if (conveyer.isSyncCompleted(round)) {
        csdebug() << "NODE> Packets sync completed, #" << round;

        if (roundPackageCache_.size() > 0) {
            auto rPackage = roundPackageCache_.back();
            csdebug() << "NODE> Run characteristic meta";
            getCharacteristic(rPackage);
        }
        else {
            csdebug() << "NODE> There is no roundPackage in the list, return and await any";
            return;
        }

        // if next block maybe stored, the last written sequence maybe updated, so deferred consensus maybe resumed
        if (blockChain_.getLastSeq() + 1 == cs::Conveyer::instance().currentRoundNumber()) {
            csdebug() << "NODE> got all blocks written in current round";
            startConsensus();
        }
    }
}

void Node::processTransactionsPacket(cs::TransactionsPacket&& packet) {
    cs::Conveyer::instance().addTransactionsPacket(packet);
}

void Node::reviewConveyerHashes() {
    const cs::Conveyer& conveyer = cs::Conveyer::instance();
    const bool isHashesEmpty = conveyer.currentRoundTable().hashes.empty();

    if (!isHashesEmpty && !conveyer.isSyncCompleted()) {
        sendPacketHashesRequest(conveyer.currentNeededHashes(), conveyer.currentRoundNumber(), startPacketRequestPoint_);
        return;
    }

    if (isHashesEmpty) {
        csdebug() << "NODE> No hashes in round table, start consensus now";
    }
    else {
        csdebug() << "NODE> All hashes in conveyer, start consensus now";
    }

    startConsensus();
}

void Node::processSync() {
    if (!cs::ConfigHolder::instance().config()->isSyncOn()) {
        return;
    }
    const auto lastSequence = blockChain_.getLastSeq();
    const auto round = cs::Conveyer::instance().currentRoundNumber();

    if (stat_.isCurrentRoundTooLong(kLastPoolSynchroDelay_) && round < lastSequence + cs::PoolSynchronizer::kRoundDifferentForSync) {
        poolSynchronizer_->syncLastPool();
    }
    else {
        poolSynchronizer_->sync(round, cs::PoolSynchronizer::kRoundDifferentForSync);
    }
}

void Node::specialSync(cs::Sequence finSeq, cs::PublicKey& source) {
    //csinfo() << "Will synchronize till " << finSeq;
    //poolSynchronizer_->syncTill(finSeq, source, true);
    //csinfo() << "Last blockchain sequence: " << getBlockChain().getLastSeq();
}

void Node::setTop(cs::Sequence finSeq) {
    //csinfo() << "Blockchain top will be: " << finSeq;
    //while (getBlockChain().getLastSeq() > finSeq) {
    //    getBlockChain().removeLastBlock();
    //}

    //csinfo() << "Last blockchain sequence: " << getBlockChain().getLastSeq();
}

void Node::showNeighbours() {
    /*poolSynchronizer_->showNeighbours();*/
}

void Node::setIdle() {

}

void Node::setWorking() {

}

void Node::showDbParams() {
    getBlockChain().showDBParams();
}

void Node::addToBlackList(const cs::PublicKey& key, bool isMarked) {
    if (isMarked) {
        transport_->ban(key);
        cswarning() << "Neigbour " << cs::Utils::byteStreamToHex(key) << " added to network black list";
        EventReport::sendBlackListUpdate(*this, key, true /*added*/);
    }
    else {
        transport_->revertBan(key);
        cswarning() << "Neigbour " << cs::Utils::byteStreamToHex(key) << " released from network black list";
        EventReport::sendBlackListUpdate(*this, key, false /*erased*/);

    }
}

bool Node::isPoolsSyncroStarted() {
    return poolSynchronizer_->isSyncroStarted();
}

std::optional<cs::TrustedConfirmation> Node::getConfirmation(cs::RoundNumber round) const {
    return confirmationList_.find(round);
}

void Node::processTimer() {
    cs::Conveyer& conveyer = cs::Conveyer::instance();
    const auto round = conveyer.currentRoundNumber();

    if (round <= cs::TransactionsFlushRound) {
        return;
    }

    conveyer.flushTransactions();
}

void Node::onTransactionsPacketFlushed(const cs::TransactionsPacket& packet) {
    if (cs::Conveyer::instance().currentRoundNumber() < Consensus::syncroChangeRound) {
        CallsQueue::instance().insert(std::bind(&Node::sendTransactionsPacket, this, packet));
    }
    else {
        CallsQueue::instance().insert(std::bind(&Node::sendTransactionsPacketHash, this, packet));
    }


}

void Node::onPingChecked(cs::Sequence sequence, const cs::PublicKey& sender) {
    auto lastSequence = blockChain_.getLastSeq();

    if (lastSequence < sequence) {
        cswarning() << "Local max block " << WithDelimiters(lastSequence) << " is lower than remote one " << WithDelimiters(sequence)
                    << ", trying to request round table";

        CallsQueue::instance().insert([=] {
            roundPackRequest(sender, sequence);
        });
    }
}

// thread safe slot (called from poolSynchronizer_)
void Node::sendBlockRequest(const cs::PublicKey& target, const cs::PoolsRequestedSequences& sequences) {
    status_ = cs::NodeStatus::Synchronization;
    const auto round = cs::Conveyer::instance().currentRoundNumber();
    csmeta(csdetails) << "Target out(): " << ", sequence from: " << sequences.front()
                      << ", to: " << sequences.back() << ", round: " << round;

    BaseFlags flags = static_cast<BaseFlags>(BaseFlags::Signed | BaseFlags::Compressed);
    transport_->sendDirect(formPacket(flags, MsgTypes::BlockRequest, round, sequences), target);
}

void Node::sendBlockRequestToConfidants(cs::Sequence sequence) {
    cslog() << kLogPrefix_ << "request block " << WithDelimiters(sequence) << " from trusted";
    const auto round = cs::Conveyer::instance().currentRoundNumber();
    cs::PoolsRequestedSequences sequences;
    sequences.push_back(sequence);

    // try to send to confidants..
    const auto& confidants = cs::Conveyer::instance().confidants();
    sendDirect(confidants, MsgTypes::BlockRequest, round, sequences, size_t(0) /*packetNum*/);
}

Node::MessageActions Node::chooseMessageAction(const cs::RoundNumber rNum, const MsgTypes type, const cs::PublicKey sender) {
    if (!good_) {
        return MessageActions::Drop;
    }

    // always process this types
    switch (type) {
        case MsgTypes::FirstSmartStage:
        case MsgTypes::SecondSmartStage:
        case MsgTypes::ThirdSmartStage:
        case MsgTypes::NodeStopRequest:
        case MsgTypes::TransactionPacket:
        case MsgTypes::TransactionPacketHash:
        case MsgTypes::TransactionsPacketRequest:
        case MsgTypes::TransactionsPacketReply:
        case MsgTypes::TransactionsPacketBaseRequest:
        case MsgTypes::TransactionsPacketBaseReply:
        case MsgTypes::RoundTableRequest:
        case MsgTypes::RejectedContracts:
        case MsgTypes::RoundPackRequest:
        case MsgTypes::EmptyRoundPack:
        case MsgTypes::StateRequest:
        case MsgTypes::StateReply:
        case MsgTypes::BlockAlarm:
        case MsgTypes::EventReport:
        case MsgTypes::SyncroMsg:
            return MessageActions::Process;

        default:
            break;
    }

    const auto round = cs::Conveyer::instance().currentRoundNumber();

    // starts next round, otherwise
    if (type == MsgTypes::RoundTable) {
        if (rNum >= round) {
            return MessageActions::Process;
        }

        // TODO: detect absence of proper current round info (round may be set by SS or BB)
        return MessageActions::Drop;
    }

    // BB: every round (for now) may be handled:
    if (type == MsgTypes::BootstrapTable) {
        return MessageActions::Process;
    }

    if (type == MsgTypes::Utility) {
        return (round < rNum + Consensus::UtilityMessageRoundInterval && round + Consensus::UtilityMessageRoundInterval > rNum ) ? MessageActions::Process : MessageActions::Drop;
    }

    if (type == MsgTypes::BlockRequest || type == MsgTypes::RequestedBlock) {
        // which round would not be on the remote we may require the requested block or get block request
        return MessageActions::Process;
    }

    if (type == MsgTypes::RoundTableReply) {
        return (rNum >= round ? MessageActions::Process : MessageActions::Drop);
    }

    if (type == MsgTypes::BlockHash) {
        if (rNum < round) {
            // outdated
            return MessageActions::Drop;
        }

        if (rNum > blockChain_.getLastSeq() + Consensus::MaxRoundsCancelContract) {
            // too many rounds behind the global round
            return MessageActions::Drop;
        }

        if (rNum > round) {
            csdebug() << "NODE> outrunning block hash (#" << rNum << ") is postponed until get round info";
            return MessageActions::Postpone;
        }

        if (!cs::Conveyer::instance().isSyncCompleted()) {
            csdebug() << "NODE> block hash is postponed until conveyer sync is completed";
            return MessageActions::Postpone;
        }

        // in time
        return MessageActions::Process;
    }

    if (rNum < round) {
        return type == MsgTypes::NewBlock ? MessageActions::Process : MessageActions::Drop;
    }

    // outrunning packets mean round lag
    if (rNum > round) {
        if (rNum - round == 1) {
            // wait for next round
            return MessageActions::Postpone;
        }
        else {
            // more then 1 round lag, request round info
            if (round > 1 && subRound_ == 0) {
                // not on the very start
                cswarning() << "NODE> detect round lag (global " << rNum << ", local " << round << ")";
                roundPackRequest(sender, rNum);
            }

            return MessageActions::Drop;
        }
    }

    // (rNum == round) => handle now
    return MessageActions::Process;
}

void Node::updateConfigFromFile() {
    observer_.notify();
}

static const char* nodeLevelToString(Node::Level nodeLevel) {
    switch (nodeLevel) {
    case Node::Level::Normal:
        return "Normal";
    case Node::Level::Confidant:
        return "Confidant";
    case Node::Level::Main:
        return "Main";
    case Node::Level::Writer:
        return "Writer";
    }

    return "UNKNOWN";
}

std::ostream& operator<<(std::ostream& os, Node::Level nodeLevel) {
    os << nodeLevelToString(nodeLevel);
    return os;
}

template <typename... Args>
void Node::sendDirect(const cs::PublicKey& target, const MsgTypes msgType, const cs::RoundNumber round, Args&&... args) {
    csdetails() << "NODE> Sending Direct data, round " << round
                << "msgType: " << Packet::messageTypeToString(msgType);

    transport_->sendDirect(formPacket(BaseFlags::Compressed, msgType, round, args...), target);
}

template <typename... Args>
void Node::sendDirect(const cs::PublicKeys& keys, const MsgTypes msgType, const cs::RoundNumber round, Args&&... args) {
    csdebug() << "NODE> Sending direct to list, round: " << round
              << ", msgType: " << Packet::messageTypeToString(msgType);

    transport_->sendMulticast(formPacket(BaseFlags::Compressed, msgType, round, args...), keys);
}

template <class... Args>
void Node::sendBroadcast(const MsgTypes msgType, const cs::RoundNumber round, Args&&... args) {
    csdetails() << "NODE> Sending broadcast, round: " << round
                << ", msgType: " << Packet::messageTypeToString(msgType);

    transport_->sendBroadcast(formPacket(BaseFlags::Compressed, msgType, round, args...));
}

template <class... Args>
void Node::sendBroadcastIfNoConnection(const cs::PublicKey& target, const MsgTypes msgType, const cs::RoundNumber round, Args&&... args) {
    csdetails() << "NODE> Sending broadcast IF NO CONNECTION, round: " << round
                << ", msgType: " << Packet::messageTypeToString(msgType);

    transport_->sendBroadcastIfNoConnection(formPacket(BaseFlags::Compressed, msgType, round, args...), target);
}

template <class... Args>
void Node::sendBroadcastIfNoConnection(const cs::PublicKeys& keys, const MsgTypes msgType, const cs::RoundNumber round, Args&&... args) {
    csdetails() << "NODE> Sending broadcast IF NO CONNECTION, round: " << round
                << ", msgType: " << Packet::messageTypeToString(msgType);

    for (auto& key : keys) {
        transport_->sendBroadcastIfNoConnection(formPacket(BaseFlags::Compressed, msgType, round, args...), key);
    }
}

template <class... Args>
void Node::sendConfidants(const MsgTypes msgType, const cs::RoundNumber round, Args&&... args) {
    csdebug() << "NODE> Sending confidants, round: " << round
              << ", msgType: " << Packet::messageTypeToString(msgType);

    sendBroadcastIfNoConnection(cs::Conveyer::instance().confidants(), msgType, round, std::forward<Args>(args)...);
}

void Node::sendStageOne(const cs::StageOne& stageOneInfo) {
    if (myLevel_ != Level::Confidant) {
        cswarning() << "NODE> Only confidant nodes can send consensus stages";
        return;
    }

    csmeta(csdebug) << "Round: " << cs::Conveyer::instance().currentRoundNumber() << "." << cs::numeric_cast<int>(subRound_)
        << cs::StageOne::toString(stageOneInfo);

    csdebug() << "Stage one Message R-" << cs::Conveyer::instance().currentRoundNumber() << "[" << static_cast<int>(stageOneInfo.sender)
        << "]";// : " << cs::Utils::byteStreamToHex(stageOneInfo.message.data(), stageOneInfo.message.size());
    csdebug() << "Stage one Signature R-" << cs::Conveyer::instance().currentRoundNumber() << "[" << static_cast<int>(stageOneInfo.sender)
        << "]";// : " << cs::Utils::byteStreamToHex(stageOneInfo.signature.data(), stageOneInfo.signature.size());

    sendConfidants(MsgTypes::FirstStage, cs::Conveyer::instance().currentRoundNumber(), subRound_, stageOneInfo.signature, stageOneInfo.message);

    csmeta(csdetails) << "Sent message size " << stageOneInfo.message.size();
}

void Node::getStageOne(const uint8_t* data, const size_t size, const cs::PublicKey& sender) {
    if (size < Consensus::StageOneMinimumSize && size > Consensus::StageOneMaximumSize) {
        csdebug() << kLogPrefix_ << "Invalid stage One size: " << size;
        return;
    }
    csmeta(csdetails) << "started";

    if (myLevel_ != Level::Confidant) {
        csdebug() << kLogPrefix_ << "ignore stage-1 as no confidant, stage sender: " << cs::Utils::byteStreamToHex(sender);
        return;
    }

    cs::IDataStream stream(data, size);

    uint8_t subRound = 0;
    stream >> subRound;

    if (subRound != subRound_) {
        cswarning() << kLogPrefix_ << "ignore stage-1 with subround #" << static_cast<int>(subRound) << ", required #" << static_cast<int>(subRound_);
        return;
    }

    cs::StageOne stage;
    stream >> stage.signature;
    stream >> stage.message;

    if (!stream.isValid() || !stream.isEmpty()) {
        csmeta(cserror) << "Bad stage-1 packet format";
        return;
    }
    csdetails() << kLogPrefix_ << "Stage1 message: " << cs::Utils::byteStreamToHex(stage.message);
    csdetails() << kLogPrefix_ << "Stage1 signature: " << cs::Utils::byteStreamToHex(stage.signature);

    // hash of part received message
    stage.messageHash = cscrypto::calculateHash(stage.message.data(), stage.message.size());
    const cs::Conveyer& conveyer = cs::Conveyer::instance();

    cs::Bytes signedMessage;
    cs::ODataStream signedStream(signedMessage);
    signedStream << conveyer.currentRoundNumber();
    signedStream << subRound_;
    signedStream << stage.messageHash;

    // stream for main message
    cs::IDataStream stageStream(stage.message.data(), stage.message.size());
    stageStream >> stage.sender;
    stageStream >> stage.hash;
    stageStream >> stage.trustedCandidates;
    stageStream >> stage.hashesCandidates;
    stageStream >> stage.roundTimeStamp;

    if (!conveyer.isConfidantExists(stage.sender)) {
        return;
    }

    const cs::PublicKey& confidant = conveyer.confidantByIndex(stage.sender);
       if (!cscrypto::verifySignature(stage.signature, confidant, signedMessage.data(), signedMessage.size())) {
        cswarning() << kLogPrefix_ << "StageOne from T[" << static_cast<int>(stage.sender) << "] -  WRONG SIGNATURE!!! Message: " 
            << cs::Utils::byteStreamToHex(stage.message.data(), stage.message.size());
        return;
    }

    if (confidant != sender) {
        csmeta(csdebug) << kLogPrefix_ << "StageOne of " << getSenderText(confidant) << " sent by " << getSenderText(sender);
    }
    else {
        csmeta(csdebug) << kLogPrefix_ << "StageOne of T[" << static_cast<int>(stage.sender) << "], sender key ok";
    }

    csdetails() << csname() << "Hash: " << cs::Utils::byteStreamToHex(stage.hash.data(), stage.hash.size());

    solver_->gotStageOne(std::move(stage));
}

void Node::sendStageTwo(cs::StageTwo& stageTwoInfo) {
    csmeta(csdetails) << "started";

    if (myLevel_ != Level::Confidant && myLevel_ != Level::Writer) {
        cswarning() << "Only confidant nodes can send consensus stages";
        return;
    }

    sendConfidants(MsgTypes::SecondStage, cs::Conveyer::instance().currentRoundNumber(), subRound_, stageTwoInfo.signature, stageTwoInfo.message);

    // cash our stage two
    csmeta(csdetails) << "Bytes size " << stageTwoInfo.message.size() << " ... done";
}

void Node::getStageTwo(const uint8_t* data, const size_t size, const cs::PublicKey& sender) {
    if (size < Consensus::StageTwoMinimumSize && size > Consensus::StageTwoMaximumSize) {
        csdebug() << kLogPrefix_ << "Invalid stage Two size: " << size;
        return;
    }
    csmeta(csdetails);

    if (myLevel_ != Level::Confidant && myLevel_ != Level::Writer) {
        csdebug() << kLogPrefix_ << "Ignore StageTwo as no confidant, stage sender: " << cs::Utils::byteStreamToHex(sender);
        return;
    }

    csdebug() << kLogPrefix_ << "Getting StageTwo from " << getSenderText(sender);

    cs::IDataStream stream(data, size);

    uint8_t subRound = 0;
    stream >> subRound;

    if (subRound != subRound_) {
        cswarning() << kLogPrefix_ << "Ignore StageTwo with subround #" << static_cast<int>(subRound) << ", required #" << static_cast<int>(subRound_);
        return;
    }

    cs::StageTwo stage;
    stream >> stage.signature;

    cs::Bytes bytes;
    stream >> bytes;

    if (!stream.isValid() || !stream.isEmpty()) {
        cserror() << "NODE> Bad stage-2 packet format";
        return;
    }

    cs::IDataStream stageStream(bytes.data(), bytes.size());
    stageStream >> stage.sender;
    stageStream >> stage.signatures;
    stageStream >> stage.hashes;

    const cs::Conveyer& conveyer = cs::Conveyer::instance();

    if (!conveyer.isConfidantExists(stage.sender)) {
        return;
    }

    if (!cscrypto::verifySignature(stage.signature, conveyer.confidantByIndex(stage.sender), bytes.data(), bytes.size())) {
        csdebug() << kLogPrefix_ << "StageTwo [" << static_cast<int>(stage.sender) << "] -  WRONG SIGNATURE!!! Message: " 
            << cs::Utils::byteStreamToHex(bytes.data(), bytes.size());
        return;
    }

    csmeta(csdetails) << "Signature is OK";
    stage.message = std::move(bytes);

    csdebug() << kLogPrefix_ << "StageTwo [" << static_cast<int>(stage.sender) << "] is OK!";
    solver_->gotStageTwo(stage);
}

void Node::sendStageThree(cs::StageThree& stageThreeInfo) {
    csdebug() << __func__;

    if (myLevel_ != Level::Confidant) {
        cswarning() << "NODE> Only confidant nodes can send consensus stages";
        return;
    }

    // TODO: think how to improve this code
    sendConfidants(MsgTypes::ThirdStage, cs::Conveyer::instance().currentRoundNumber(), subRound_, stageThreeInfo.signature, stageThreeInfo.message);

    // cach stage three
    csmeta(csdetails) << "bytes size " << stageThreeInfo.message.size();
    stageThreeSent_ = true;
    csmeta(csdetails) << "done";
}

void Node::getStageThree(const uint8_t* data, const size_t size, const cs::PublicKey& sender) {
    if (size < Consensus::StageThreeMinimumSize && size > Consensus::StageThreeMaximumSize) {
        csdebug() << kLogPrefix_ << "Invalid stage Two size: " << size;
        return;
    }
    csmeta(csdetails);

    if (myLevel_ != Level::Confidant && myLevel_ != Level::Writer) {
        csdebug() << "NODE> ignore stage-3 as no confidant, stage sender: " << cs::Utils::byteStreamToHex(sender);
        return;
    }

    cs::IDataStream stream(data, size);
    uint8_t subRound = 0;
    stream >> subRound;

    if (subRound != subRound_) {
        cswarning() << "NODE> ignore stage-3 with subround #" << static_cast<int>(subRound) << ", required #" << static_cast<int>(subRound_);
        return;
    }

    cs::StageThree stage;
    stream >> stage.signature;

    cs::Bytes bytes;
    stream >> bytes;

    if (!stream.isValid() || !stream.isEmpty()) {
        cserror() << "NODE> Bad stage-3 packet format. Packet received from: " << cs::Utils::byteStreamToHex(sender.data(), sender.size());
        return;
    }

    cs::IDataStream stageStream(bytes.data(), bytes.size());
    stageStream >> stage.sender;
    stageStream >> stage.writer;
    stageStream >> stage.iteration;  // this is a potential problem!!!
    stageStream >> stage.blockSignature;
    stageStream >> stage.roundSignature;
    stageStream >> stage.trustedSignature;
    stageStream >> stage.realTrustedMask;

    const cs::Conveyer& conveyer = cs::Conveyer::instance();

    if (!conveyer.isConfidantExists(stage.sender)) {
        return;
    }

    if (!conveyer.isConfidantExists(stage.writer)) {
        return;
    }

    if (stage.iteration < solver_->currentStage3iteration()) {
        stageRequest(MsgTypes::ThirdStageRequest, myConfidantIndex_, stage.sender, solver_->currentStage3iteration());
        return;
    }
    else if (stage.iteration > solver_->currentStage3iteration()) {
        // store
        return;
    }

    if (!cscrypto::verifySignature(stage.signature, conveyer.confidantByIndex(stage.sender), bytes.data(), bytes.size())) {
        cswarning() << kLogPrefix_ << "stageThree from T[" << static_cast<int>(stage.sender) << "] -  WRONG SIGNATURE!!! Message: " 
            << cs::Utils::byteStreamToHex(bytes.data(), bytes.size());
        return;
    }

    stage.message = std::move(bytes);

    csdebug() << "NODE> stage-3 from T[" << static_cast<int>(stage.sender) << "] - preliminary check ... passed!";
    solver_->gotStageThree(std::move(stage), (stageThreeSent_ ? 2 : 0));
}

void Node::adjustStageThreeStorage() {
    stageThreeSent_ = false;
}

void Node::stageRequest(MsgTypes msgType, uint8_t respondent, uint8_t required, uint8_t iteration) {
    csmeta(csdebug) << "started";

    if (myLevel_ != Level::Confidant && myLevel_ != Level::Writer) {
        cswarning() << kLogPrefix_ << "Only confidant nodes can request consensus stages";
        return;
    }

    switch (msgType) {
    case MsgTypes::FirstStageRequest:
    case MsgTypes::SecondStageRequest:
    case MsgTypes::ThirdStageRequest:
        break;
    default:
        csdebug() << kLogPrefix_ << "Illegal call to method: stageRequest(), ignore";
        return;
    }

    const cs::Conveyer& conveyer = cs::Conveyer::instance();

    if (!conveyer.isConfidantExists(respondent)) {
        return;
    }

    sendBroadcastIfNoConnection(conveyer.confidantByIndex(respondent), msgType, cs::Conveyer::instance().currentRoundNumber(), subRound_, myConfidantIndex_, required, iteration);

    csmeta(csdetails) << "done";
}

void Node::getStageRequest(const MsgTypes msgType, const uint8_t* data, const size_t size, const cs::PublicKey& requester) {
    csmeta(csdebug) << "started";

    if (myLevel_ != Level::Confidant) {
        return;
    }

    cs::IDataStream stream(data, size);

    uint8_t subRound = 0;
    stream >> subRound;

    if (subRound != subRound_) {
        cswarning() << kLogPrefix_ << "We got StageRequest for the Node with SUBROUND, we don't have";
        return;
    }

    uint8_t requesterNumber = 0;
    stream >> requesterNumber;

    uint8_t requiredNumber = 0;
    stream >> requiredNumber;

    uint8_t iteration = solver_->currentStage3iteration(); // default value

    if (stream.isAvailable(1)) {
        stream >> iteration;
    }

    if (!stream.isValid() || !stream.isEmpty()) {
        cserror() << "Bad StageRequest packet format";
        return;
    }

    const cs::Conveyer& conveyer = cs::Conveyer::instance();

    if (!conveyer.isConfidantExists(requesterNumber) || requester != conveyer.confidantByIndex(requesterNumber)) {
        return;
    }

    switch (msgType) {
        case MsgTypes::FirstStageRequest:
            solver_->gotStageOneRequest(requesterNumber, requiredNumber);
            break;
        case MsgTypes::SecondStageRequest:
            solver_->gotStageTwoRequest(requesterNumber, requiredNumber);
            break;
        case MsgTypes::ThirdStageRequest:
            solver_->gotStageThreeRequest(requesterNumber, requiredNumber, iteration);
            break;
        default:
            break;
    }
}

void Node::sendStageReply(const uint8_t sender, const cs::Signature& signature, const MsgTypes msgType, const uint8_t requester, cs::Bytes& message) {
    csmeta(csdebug) << "started";

    if (myLevel_ != Level::Confidant) {
        cswarning() << kLogPrefix_ << "Only confidant nodes can send consensus stages";
        return;
    }

    const cs::Conveyer& conveyer = cs::Conveyer::instance();

    if (!conveyer.isConfidantExists(requester) || !conveyer.isConfidantExists(sender)) {
        return;
    }

    sendBroadcastIfNoConnection(conveyer.confidantByIndex(requester), msgType, cs::Conveyer::instance().currentRoundNumber(), subRound_, signature, message);
    csmeta(csdetails) << "done";
}

void Node::sendSmartReject(const std::vector<RefExecution>& rejectList) {
    if (myLevel_ != Level::Confidant) {
        cswarning() << "NODE> Only confidant nodes can send smart Reject messages";
        return;
    }
    if (rejectList.empty()) {
        csmeta(cserror) << "must not send empty rejected contracts pack";
        return;
    }

    cs::Bytes data;
    cs::ODataStream stream(data);

    stream << rejectList;

    csdebug() << "Node: sending " << rejectList.size() << " rejected contract(s) to related smart confidants";
    sendBroadcast(MsgTypes::RejectedContracts, cs::Conveyer::instance().currentRoundNumber(), data);
}

void Node::getSmartReject(const uint8_t* data, const size_t size, const cs::RoundNumber rNum, const cs::PublicKey& sender) {
    csunused(rNum);
    csunused(sender);

    cs::IDataStream stream(data, size);

    cs::Bytes bytes;
    stream >> bytes;

    cs::IDataStream smartStream(bytes.data(), bytes.size());

    std::vector<RefExecution> rejectList;
    stream >> rejectList;

    if (!stream.isValid() || stream.isAvailable(1)) {
        return;
    }

    if (rejectList.empty()) {
        csmeta(cserror) << "empty rejected contracts pack received";
        return;
    }

    csdebug() << "Node: " << rejectList.size() << " rejected contract(s) received";
    emit gotRejectedContracts(rejectList);
}

void Node::sendSmartStageOne(const cs::ConfidantsKeys& smartConfidants, const cs::StageOneSmarts& stageOneInfo) {
    csmeta(csdebug) << "started";

    if (std::find(smartConfidants.cbegin(), smartConfidants.cend(), solver_->getPublicKey()) == smartConfidants.cend()) {
        cswarning() << "NODE> Only confidant nodes can send smart-contract consensus stages";
        return;
    }

    csmeta(csdetails) << std::endl
                      << "Smart starting Round: " << cs::SmartConsensus::blockPart(stageOneInfo.id) << '.' << cs::SmartConsensus::transactionPart(stageOneInfo.id) << std::endl
                      << "Sender: " << static_cast<int>(stageOneInfo.sender) << std::endl
                      << "Hash: " << cs::Utils::byteStreamToHex(stageOneInfo.hash.data(), stageOneInfo.hash.size());

    sendBroadcastIfNoConnection(smartConfidants, MsgTypes::FirstSmartStage, cs::Conveyer::instance().currentRoundNumber(),
               stageOneInfo.message, stageOneInfo.signature);

    csmeta(csdebug) << "done";
}

bool Node::canSaveSmartStages(cs::Sequence seq, cs::PublicKey key) {
    cs::Sequence curSequence = getBlockChain().getLastSeq();

    if (curSequence + 1 < cs::Conveyer::instance().currentRoundNumber()) {
        return false;
    }

    if (seq <= getBlockChain().getLastSeq()) {
        auto pool = getBlockChain().loadBlock(seq);
        if (pool.is_valid()) {
            auto it = std::find(pool.confidants().cbegin(), pool.confidants().cend(), key);
            if (it != pool.confidants().cend()) {
                return true;
            }
        }
        return false;
    }
    csdebug() << "Strange situation: better save for any sake";
    return true;
}

void Node::getSmartStageOne(const uint8_t* data, const size_t size, const cs::RoundNumber, const cs::PublicKey& sender) {
    if (!canBeTrusted(true)) {
        return;
    }
    csdebug() << __func__ << ": starting";
    
    cs::IDataStream stream(data, size);

    cs::StageOneSmarts stage;
    stream >> stage.message >> stage.signature;

    if (!stream.isValid() || !stream.isEmpty()) {
        cserror() << "Bad Smart Stage One packet format";
        return;
    }

    if (stage.signature == cs::Zero::signature) {
        csdebug() << "NODE> Sender " << cs::Utils::byteStreamToHex(sender.data(), sender.size()) << " sent unsigned smart stage One";
        return;
    }
    // hash of part received message
    stage.messageHash = cscrypto::calculateHash(stage.message.data(), stage.message.size());
    if (!stage.fillFromBinary()) {
        return;
    }
    cs::Sequence block = cs::SmartConsensus::blockPart(stage.id);
    uint32_t transaction = cs::SmartConsensus::transactionPart(stage.id);
    csdebug() << "SmartStageOne messageHash: " << cs::Utils::byteStreamToHex(stage.messageHash.data(), stage.messageHash.size());
    csdebug() << __func__ << ": starting {" << block << '.' << transaction << '}';
    
    csmeta(csdebug) << "Sender: " << static_cast<int>(stage.sender) << ", sender key: " << cs::Utils::byteStreamToHex(sender.data(), sender.size()) << std::endl
                    << "Smart#: {" << block << '.' << transaction << '}';
    csdebug() << "Hash: " << cs::Utils::byteStreamToHex(stage.hash.data(), stage.hash.size());

    //if (std::find(activeSmartConsensuses_.cbegin(), activeSmartConsensuses_.cend(), stage.id) == activeSmartConsensuses_.cend() && canSaveSmartStages(block, nodeIdKey_)) {
    //    csdebug() << "The SmartConsensus {" << block << '.' << transaction << "} is not active now, storing the stage";
    //    smartStageOneStorage_.push_back(stage);
    //    return;
    //}

    emit gotSmartStageOne(stage, false);
}

void Node::sendSmartStageTwo(const cs::ConfidantsKeys& smartConfidants, cs::StageTwoSmarts& stageTwoInfo) {
    csmeta(csdebug) << "started";

    if (std::find(smartConfidants.cbegin(), smartConfidants.cend(), solver_->getPublicKey()) == smartConfidants.cend()) {
        cswarning() << "NODE> Only confidant nodes can send smart-contract consensus stages";
        return;
    }

    // TODO: fix it by logic changing

    size_t confidantsCount = cs::Conveyer::instance().confidantsCount();
    size_t stageBytesSize = sizeof(stageTwoInfo.sender) + (sizeof(cs::Signature) + sizeof(cs::Hash)) * confidantsCount;

    cs::Bytes bytes;
    bytes.reserve(stageBytesSize);

    cs::ODataStream stream(bytes);
    stream << stageTwoInfo.sender;
    stream << stageTwoInfo.id;
    stream << stageTwoInfo.signatures;
    stream << stageTwoInfo.hashes;

    // create signature
    stageTwoInfo.signature = cscrypto::generateSignature(solver_->getPrivateKey(), bytes.data(), bytes.size());
    sendBroadcastIfNoConnection(smartConfidants, MsgTypes::SecondSmartStage, cs::Conveyer::instance().currentRoundNumber(), bytes, stageTwoInfo.signature);

    // cash our stage two
    stageTwoInfo.message = std::move(bytes);
    csmeta(csdebug) << "done";
}

void Node::getSmartStageTwo(const uint8_t* data, const size_t size, const cs::RoundNumber, const cs::PublicKey& sender) {
    if (!canBeTrusted(true)) {
        return;
    }
    csmeta(csdebug);

    csdebug() << "NODE> Getting SmartStage Two from " << cs::Utils::byteStreamToHex(sender.data(), sender.size());

    cs::IDataStream stream(data, size);

    cs::StageTwoSmarts stage;
    stream >> stage.message >> stage.signature;

    if (stage.signature == cs::Zero::signature) {
        csdebug() << "NODE> Sender " << cs::Utils::byteStreamToHex(sender.data(), sender.size()) << " sent unsigned smart stage Two";
        return;
    }

    if (!stream.isValid() || !stream.isEmpty()) {
        cserror() << "NODE> Bad SmartStageTwo packet format";
        return;
    }

    cs::IDataStream stageStream(stage.message.data(), stage.message.size());
    stageStream >> stage.sender;
    stageStream >> stage.id;
    stageStream >> stage.signatures;
    stageStream >> stage.hashes;

    csdebug() << "NODE> Read all data from the stream";

    emit gotSmartStageTwo(stage, false);
}

void Node::sendSmartStageThree(const cs::ConfidantsKeys& smartConfidants, cs::StageThreeSmarts& stageThreeInfo) {
    csmeta(csdebug) << "started";

    if (std::find(smartConfidants.cbegin(), smartConfidants.cend(), solver_->getPublicKey()) == smartConfidants.cend()) {
        cswarning() << "NODE> Only confidant nodes can send smart-contract consensus stages";
        return;
    }

    // TODO: think how to improve this code
    size_t stageSize = 2 * sizeof(cs::Byte) + stageThreeInfo.realTrustedMask.size() + stageThreeInfo.packageSignature.size();

    cs::Bytes bytes;
    bytes.reserve(stageSize);

    cs::ODataStream stream(bytes);
    stream << stageThreeInfo.sender;
    stream << stageThreeInfo.writer;
    stream << stageThreeInfo.id;
    stream << stageThreeInfo.realTrustedMask;
    stream << stageThreeInfo.packageSignature;

    stageThreeInfo.signature = cscrypto::generateSignature(solver_->getPrivateKey(), bytes.data(), bytes.size());

    sendBroadcastIfNoConnection(smartConfidants, MsgTypes::ThirdSmartStage, cs::Conveyer::instance().currentRoundNumber(),
               // payload:
              bytes, stageThreeInfo.signature);

    // cach stage three
    stageThreeInfo.message = std::move(bytes);
    csmeta(csdebug) << "done";
}

void Node::getSmartStageThree(const uint8_t* data, const size_t size, const cs::RoundNumber, const cs::PublicKey& sender) {
    if (!canBeTrusted(true)) {
        return;
    }
    csmeta(csdetails) << "started";
    csunused(sender);

    cs::IDataStream stream(data, size);

    cs::StageThreeSmarts stage;
    stream >> stage.message >> stage.signature;

    if (stage.signature == cs::Zero::signature) {
        csdebug() << "NODE> Sender " << cs::Utils::byteStreamToHex(sender.data(), sender.size()) << " sent unsigned smart stage Three";
        return;
    }

    if (!stream.isValid() || !stream.isEmpty()) {
        cserror() << "NODE> Bad SmartStage Three packet format";
        return;
    }

    cs::IDataStream stageStream(stage.message.data(), stage.message.size());
    stageStream >> stage.sender;
    stageStream >> stage.writer;
    stageStream >> stage.id;
    stageStream >> stage.realTrustedMask;
    stageStream >> stage.packageSignature;

    emit gotSmartStageThree(stage, false);
}

bool Node::smartStageRequest(MsgTypes msgType, uint64_t smartID, const cs::PublicKey& confidant, uint8_t respondent, uint8_t required) {
    csmeta(csdebug) << __func__ << "started";
    sendBroadcastIfNoConnection(confidant, msgType, cs::Conveyer::instance().currentRoundNumber(), smartID, respondent, required);
    csmeta(csdebug) << "done";
    return true;
}

void Node::getSmartStageRequest(const MsgTypes msgType, const uint8_t* data, const size_t size, const cs::PublicKey& requester) {
    csmeta(csdebug) << __func__ << "started";

    cs::IDataStream stream(data, size);

    uint8_t requesterNumber = 0;
    uint64_t smartID = 0;
    stream >> smartID >> requesterNumber;

    uint8_t requiredNumber = 0;
    stream >> requiredNumber;

    if (!stream.isValid() || !stream.isEmpty()) {
        cserror() << "Bad SmartStage request packet format";
        return;
    }

    emit receivedSmartStageRequest(msgType, smartID, requesterNumber, requiredNumber, requester);
}

void Node::sendSmartStageReply(const cs::Bytes& message, const cs::Signature& signature, const MsgTypes msgType, const cs::PublicKey& requester) {
    csmeta(csdebug) << "started";

    sendBroadcastIfNoConnection(requester, msgType, cs::Conveyer::instance().currentRoundNumber(), message, signature);
    csmeta(csdebug) << "done";
}

void Node::addSmartConsensus(uint64_t id) {
    if (std::find(activeSmartConsensuses_.cbegin(), activeSmartConsensuses_.cend(), id) != activeSmartConsensuses_.cend()) {
        csdebug() << "The smartConsensus for {" << cs::SmartConsensus::blockPart(id) << '.' << cs::SmartConsensus::transactionPart(id) << "} is already active";
        return;
    }

    activeSmartConsensuses_.push_back(id);
    checkForSavedSmartStages(id);
}

void Node::removeSmartConsensus(uint64_t id) {
    const auto it = std::find(activeSmartConsensuses_.cbegin(), activeSmartConsensuses_.cend(), id);
    if (it == activeSmartConsensuses_.cend()) {
        csdebug() << "The smartConsensus for {" << cs::SmartConsensus::blockPart(id) << '.' << cs::SmartConsensus::transactionPart(id) << "} is not active";
    }
    else {
        activeSmartConsensuses_.erase(it);
    }
    auto it_1 = smartStageOneStorage_.cbegin();
    while(it_1 != smartStageOneStorage_.cend()) {
        if (it_1->id == id) {
            it_1 = smartStageOneStorage_.erase(it_1);
        }
        else {
            ++it_1;
        }
    }
    auto it_2 = smartStageTwoStorage_.cbegin();
    while (it_2 != smartStageTwoStorage_.cend()) {
        if (it_2->id == id) {
            it_2 = smartStageTwoStorage_.erase(it_2);
        }
        else {
            ++it_2;
        }
    }
    auto it_3 = smartStageThreeStorage_.cbegin();
    while (it_3 != smartStageThreeStorage_.cend()) {
        if (it_3->id == id) {
            it_3 = smartStageThreeStorage_.erase(it_3);
        }
        else {
            ++it_3;
        }
    }
}

void Node::checkForSavedSmartStages(uint64_t id) {
    for (auto& it : smartStageOneStorage_) {
        if (it.id == id) {
            emit gotSmartStageOne(it, false);
        }
    }
}

//TODO: this code should be refactored
bool Node::sendRoundPackage(const cs::RoundNumber rNum, const cs::PublicKey& target) {
    csdebug() << "Send round table: ";

    if (roundPackageCache_.size() == 0) {
        csdebug() << "No active round table, so cannot send";
        return false;
    }

    auto rpCurrent = std::find_if(roundPackageCache_.begin(), roundPackageCache_.end(), [rNum](cs::RoundPackage& rp) {return rp.roundTable().round == rNum;});
    if (rpCurrent == roundPackageCache_.end()) {
        csdebug() << "Cannot find round table, so cannot send";
        return false;
    }
    bool showVersion = rpCurrent->roundTable().round >= Consensus::StartingDPOS && Consensus::miningOn;
    sendDirect(target, MsgTypes::RoundTable, rpCurrent->roundTable().round, rpCurrent->subRound(), rpCurrent->toBinary(showVersion));
    csdebug() << "Done";

    if (!rpCurrent->poolMetaInfo().characteristic.mask.empty()) {
        csmeta(csdebug) << "Packing " << rpCurrent->poolMetaInfo().characteristic.mask.size() << " bytes of char. mask to send";
    }

    return true;
}

void Node::sendRoundPackageToAll(cs::RoundPackage& rPackage) {
    // add signatures// blockSignatures, roundSignatures);
    csmeta(csdetails) << "Send round table to all";
    bool showVersion = rPackage.roundTable().round >= Consensus::StartingDPOS && Consensus::miningOn;
    sendBroadcast(MsgTypes::RoundTable, rPackage.roundTable().round, rPackage.subRound(), rPackage.toBinary(showVersion));

    if (!rPackage.poolMetaInfo().characteristic.mask.empty()) {
        csmeta(csdebug) << "Packing " << rPackage.poolMetaInfo().characteristic.mask.size() << " bytes of char. mask to send";
    }

    /////////////////////////////////////////////////////////////////////////// screen output
    csdebug() << "------------------------------------------  SendRoundTable  ---------------------------------------" 
        << std::endl << rPackage.toString()
        << "\n----------------------------------------------------------------------------------------------------";
}

void Node::sendRoundTable(cs::RoundPackage& rPackage) {
    becomeWriter();

    cs::Conveyer& conveyer = cs::Conveyer::instance();
    csdebug() << "SendRoundTable: add confirmation for round " << conveyer.currentRoundTable().round << " trusted";
    conveyer.setRound(rPackage.roundTable().round);
    
    subRound_ = 0;

    cs::RoundTable table;
    table.round = conveyer.currentRoundNumber();
    table.confidants = rPackage.roundTable().confidants;
    table.hashes = rPackage.roundTable().hashes;
    roundPackageCache_.push_back(rPackage);
    clearRPCache(rPackage.roundTable().round);
    sendRoundPackageToAll(rPackage);

    csdebug() << "Round " << rPackage.roundTable().round << ", Confidants count " << rPackage.roundTable().confidants.size();
    csdebug() << "Hashes count: " << rPackage.roundTable().hashes.size();

    performRoundPackage(rPackage, solver_->getPublicKey(), false);
}

bool Node::receivingSignatures(cs::RoundPackage& rPackage, cs::PublicKeys& currentConfidants) {
    csdebug() << "NODE> PoolSigs Amnt = " << rPackage.poolSignatures().size()
        << ", TrustedSigs Amnt = " << rPackage.trustedSignatures().size()
        << ", RoundSigs Amnt = " << rPackage.roundSignatures().size();

    if (rPackage.poolMetaInfo().realTrustedMask.size() != currentConfidants.size()) {
        csmeta(cserror) << "Illegal trusted mask count in round table: " << rPackage.poolMetaInfo().realTrustedMask.size();
        return false;
    }
    bool showVersion = rPackage.roundTable().round >= Consensus::StartingDPOS && Consensus::miningOn;
    cs::Bytes roundBytes = rPackage.bytesToSign(showVersion);
    cs::Hash tempHash = cscrypto::calculateHash(roundBytes.data(), roundBytes.size());

    if (!cs::NodeUtils::checkGroupSignature(currentConfidants, rPackage.poolMetaInfo().realTrustedMask, rPackage.roundSignatures(), tempHash)) {
        csdebug() << "NODE> The roundtable signatures are NOT OK";
        return false;
    }
    else {
        csdebug() << "NODE> The roundtable signatures are ok";
    }

    //refactored -->
    cs::Bytes bytes = rPackage.roundTable().toBinary();
    cs::Hash trustedHash = cscrypto::calculateHash(bytes.data(), bytes.size());
    //refactored <--

    if (cs::NodeUtils::checkGroupSignature(currentConfidants, rPackage.poolMetaInfo().realTrustedMask, rPackage.trustedSignatures(), trustedHash)) {
        csdebug() << "NODE> The trusted confirmation for the next round are ok";
    }
    else {
        csdebug() << "NODE> The trusted confirmation for the next round are NOT OK";
        return false;
    }

    return true;
}

bool Node::rpSpeedOk(cs::RoundPackage& rPackage) {
    cs::Conveyer& conveyer = cs::Conveyer::instance();
    if (conveyer.currentRoundNumber() > Consensus::MaxRoundTimerFree && getBlockChain().getLastSeq() > 0) {
        uint64_t lastTimeStamp;
        [[maybe_unused]] uint64_t rpTimeStamp;
        try {
            std::string lTS = getBlockChain().getLastTimeStamp();
            lastTimeStamp = std::stoull(lTS.empty() == 0 ? "0" : lTS);
        }
        catch (...) {
            csdebug() << __func__ << ": last block Timestamp was announced as zero";
            return false;
        }

        uint64_t currentTimeStamp = cs::Utils::currentTimestamp();

        try {
            rpTimeStamp = std::stoull(rPackage.poolMetaInfo().timestamp);
        }
        catch (...) {
            csdebug() << __func__ << ": just received roundPackage Timestamp was announced as zero";
            return false;
        }

        if (rPackage.roundTable().round > conveyer.currentRoundNumber() + 1) {
            uint64_t delta;
            if (lastTimeStamp > currentTimeStamp) {
                delta = lastTimeStamp - currentTimeStamp;
            }
            else {
                delta = currentTimeStamp - lastTimeStamp;
            }
            uint64_t speed = delta / (rPackage.roundTable().round - conveyer.currentRoundNumber());

            const auto ave_duration = stat_.aveRoundMs();
            if (speed < ave_duration / 10 && rPackage.roundTable().round - stat_.nodeStartRound() > Consensus::SpeedCheckRound) {
                stat_.onRoundStart(rPackage.roundTable().round, true /*skip_logs*/);
                cserror() << "drop RoundPackage created in " << speed << " ms/block, average ms/round is " << ave_duration;
                return false;
            }
        }
    }
    return true;
}

bool Node::isLastRPStakeFull(cs::RoundNumber rNum) {
    if (!roundPackageCache_.empty()) {
        auto mask = roundPackageCache_.back().poolMetaInfo().realTrustedMask;
        if (cs::Conveyer::instance().currentRoundNumber() == rNum && cs::TrustedMask::trustedSize(mask) == mask.size()) {
            csdebug() << "Trusted mask in " << __func__ << ": " << cs::TrustedMask::toString(mask);
            return true;
        }
    }
    return false;
}

void Node::getRoundTable(const uint8_t* data, const size_t size, const cs::RoundNumber rNum, const cs::PublicKey& sender) {
    csdebug() << "NODE> get round table R-" << WithDelimiters(rNum) << " from " << cs::Utils::byteStreamToHex(sender.data(), sender.size());
    if (poolSynchronizer_->getMaxNeighbourSequence() > rNum + 10) {
        csdebug() << "The processed packets are usually obsolette - try to clear transport module caches";
        transport_->clearInbox();
    }

    csmeta(csdetails) << "started";

    if (myLevel_ == Level::Writer) {
        csmeta(cserror) << "Writers don't receive round table";
        return;
    }
    cs::Conveyer& conveyer = cs::Conveyer::instance();
    if (myLevel_ == Level::Confidant || !poolSynchronizer_->isSyncroStarted()) {
        if (rNum > (conveyer.currentRoundNumber() + 1) && stat_.lastRoundMs() < Consensus::PostConsensusTimeout) {
            return;
        }
    }

    cs::IDataStream stream(data, size);

    // RoundTable evocation
    cs::Byte subRound = 0;
    stream >> subRound;

    // sync state check


    if (conveyer.currentRoundNumber() == rNum && subRound_ > subRound) {
        cswarning() << "NODE> round table SUBROUND is lesser then local one, ignore round table";
        csmeta(csdetails) << "My subRound: " << static_cast<int>(subRound_) << ", Received subRound: " << static_cast<int>(subRound);
        return;
    }

    if (isLastRPStakeFull(rNum)) {
        return;
    }

    cs::Bytes bytes;
    stream >> bytes;

    if (!stream.isValid() || !stream.isEmpty()) {
        csmeta(cserror) << "Malformed packet with round table (1)";
        return;
    }

    cs::RoundPackage rPackage;

    if (!rPackage.fromBinary(bytes, rNum, subRound)) {
        csdebug() << "NODE> RoundPackage could not be parsed";
        return;
    }

    if (!rpSpeedOk(rPackage)) {
        csdebug() << "NODE> last RoundPackage has full stake";
        return;
    }

    csdebug() << "---------------------------------- RoundPackage #" << rPackage.roundTable().round << " --------------------------------------------- \n" 
        <<  rPackage.toString() 
        <<  "\n-----------------------------------------------------------------------------------------------------------------------------";

    cs::RoundNumber storedRound = conveyer.currentRoundNumber();
    conveyer.setRound(rNum);
    bool iMode = cs::ConfigHolder::instance().config()->isIdleMode();

    if (cs::ConfigHolder::instance().config()->isSyncOn() && !iMode) {
        processSync();
    }

    if (poolSynchronizer_->isSyncroStarted() && !iMode) {
        getCharacteristic(rPackage);
    }

    //if (iMode && poolSynchronizer_->getTargetSequence() > 0ULL) {
    //    poolSynchronizer_->checkSpecialSyncProcess();
    //}

    rPackage.setSenderNode(sender);
    bool updateRound = false;
    if (currentRoundPackage_.roundTable().round == 0) {//if normal or trusted  node that got RP has probably received a new RP with not full stake
        if (roundPackageCache_.empty()) {
            roundPackageCache_.push_back(rPackage);
        }
        else {
            if (rPackage.roundTable().round == roundPackageCache_.back().roundTable().round && !stageThreeSent_) {
                auto mask = roundPackageCache_.back().poolMetaInfo().realTrustedMask;
                if (cs::TrustedMask::trustedSize(rPackage.poolMetaInfo().realTrustedMask) > cs::TrustedMask::trustedSize(mask)) {
                    csdebug() << "Current Roundpackage of " << rNum << " will be replaced by new one";
                    auto it = roundPackageCache_.end();
                    if (!roundPackageCache_.empty()) {
                        --it;
                        roundPackageCache_.erase(it);
                    }
                    roundPackageCache_.push_back(rPackage);
                    updateRound = true;
                }
                else {
                    csdebug() << "Current Roundpackage of " << rNum << " won't be replaced";
                    return;
                }
            }
            else {
                if (rPackage.roundTable().round > roundPackageCache_.back().roundTable().round) {
                    roundPackageCache_.push_back(rPackage);
                }
                else {
                    csdebug() << "Current RoundPackage of " << rNum << " won't be added to cache";
                    return;
                }
            }
        }
    }
    else {//if trusted node has probably received a new RP with not full stake, but has an RP with full one
        if (rPackage.roundTable().round == currentRoundPackage_.roundTable().round) {
            auto mask = currentRoundPackage_.poolMetaInfo().realTrustedMask;
            if (cs::TrustedMask::trustedSize(rPackage.poolMetaInfo().realTrustedMask) > cs::TrustedMask::trustedSize(mask)) {
                csdebug() << "Current Roundpackage of " << rNum << " will be replaced by new one";
                roundPackageCache_.push_back(rPackage);
            }
            else {
                csdebug() << "Throw received RP " << rNum << " using the own one";
                roundPackageCache_.push_back(currentRoundPackage_);
                rPackage = currentRoundPackage_;
            }
        }
        else {
            if (rPackage.roundTable().round > currentRoundPackage_.roundTable().round) {
                roundPackageCache_.push_back(rPackage);
            }
            else {
                csdebug() << "Current RoundPackage of " << rNum << " can't be added to cache";
                return;
            }

        }
    }
 

    clearRPCache(rPackage.roundTable().round);

    cs::Signatures poolSignatures;
    cs::PublicKeys confidants;

    if (!isBootstrapRound()) {
        if (rPackage.roundTable().round > 2/* && confirmationList_.size() > 0*/) { //Here we have problems when the trusted have the first block and the others do not!!!
            auto conf = confirmationList_.find(rPackage.roundTable().round - 1/*getBlockChain().getLastSeq() + 1*/);
            if (!conf.has_value()) {
                csdebug() << "Can't find confirmation - leave getRoundPackage()";
                confirmationList_.add(rPackage.roundTable().round, false, rPackage.roundTable().confidants, rPackage.poolMetaInfo().realTrustedMask, rPackage.trustedSignatures());
                //return;
            }
            else {
                confidants = conf.value().confidants;
            }
            if (confidants.empty()) {
                csdb::Pool tmp = getBlockChain().loadBlock(rPackage.roundTable().round - 1);
                if (tmp.confidants().empty()) {
                    csdebug() << "Can't find public keys - leave getRoundPackage()";
                    return;
                }
                else {
                    confidants = tmp.confidants();
                }
            }

            if (!receivingSignatures(rPackage, confidants) && storedRound == getBlockChain().getLastSeq()) {
                return;
            }
        }
        else {
            csdebug() << "No confirmations in the list";
        }
    }
    else {
        csdebug() << "NODE> bootstrap round, so not any confirmation available";
    }
    currentRoundTableMessage_.round = rPackage.roundTable().round;
    currentRoundTableMessage_.sender = sender;
    currentRoundTableMessage_.message = cs::Bytes(data, data + size);
    performRoundPackage(rPackage, sender, updateRound);
}

void Node::setCurrentRP(const cs::RoundPackage& rp) {
    currentRoundPackage_ = rp;
}

void Node::performRoundPackage(cs::RoundPackage& rPackage, const cs::PublicKey& /*sender*/, bool updateRound) {
    csdebug() << __func__;

    // got round package in any way, reset default round table flag
    confirmationList_.add(rPackage.roundTable().round, false, rPackage.roundTable().confidants, rPackage.poolMetaInfo().realTrustedMask, rPackage.trustedSignatures());
    cs::Conveyer& conveyer = cs::Conveyer::instance();
    cs::Bytes realTrusted = rPackage.poolMetaInfo().realTrustedMask;
    const auto ptrRT = conveyer.roundTable(rPackage.roundTable().round - 1);
    if (ptrRT != nullptr) {
        const cs::ConfidantsKeys& prevConfidants = ptrRT->confidants;
        if (!prevConfidants.empty() && realTrusted.size() == prevConfidants.size()) {
            for (size_t i = 0; i < realTrusted.size(); ++i) {
                if (realTrusted[i] == cs::ConfidantConsts::InvalidConfidantIndex) {
                    solver_->addToGraylist(prevConfidants[i], Consensus::GrayListPunishment);
                }
            }
        }
    }
    
    // update sub round and max heighbours sequence
    subRound_ = rPackage.subRound();
    cs::PacketsHashes hashes = rPackage.roundTable().hashes;
    cs::PublicKeys confidants = rPackage.roundTable().confidants;
    cs::RoundTable roundTable;
    roundTable.round = rPackage.roundTable().round;
    roundTable.confidants = std::move(confidants);
    roundTable.hashes = std::move(hashes);
    //roundTable.general = sender;

    csdebug() << "NODE> confidants: " << roundTable.confidants.size();
    
    // first change conveyer state
    cs::Conveyer::instance().setTable(roundTable);

    // create pool by previous round, then change conveyer state.
    if (!cs::ConfigHolder::instance().config()->isIdleMode()) {
        getCharacteristic(rPackage);
    }


    lastRoundPackageTime_ = cs::Utils::currentTimestamp();

    onRoundStart(cs::Conveyer::instance().currentRoundTable(), updateRound);

    currentRoundPackage_ = cs::RoundPackage();
    reviewConveyerHashes();

    if (isBootstrapRound_) {
        isBootstrapRound_ = false;
        cslog() << "NODE> Bootstrap off";
    }

    csmeta(csdetails) << "done\n";
}

bool Node::isTransactionsInputAvailable() {
    size_t justTime = cs::Utils::currentTimestamp();
    if (justTime > lastRoundPackageTime_) {
        if (justTime - lastRoundPackageTime_ > Consensus::MaxRoundDuration) {
            cslog() << "NODE> reject transaction: the current round lasts too long, possible traffic problems";
            return false; //
        }
    }
    else {
        if (lastRoundPackageTime_ - justTime > Consensus::MaxRoundDuration) {
            cslog() << "NODE> reject transaction: possible wrong node clock";
            return false;
        }
    }
    // default conditions: no sync and last block is near to current round
    const auto round = cs::Conveyer::instance().currentRoundNumber();
    const auto sequence = getBlockChain().getLastSeq();
    if(round < sequence || round - sequence >= cs::PoolSynchronizer::kRoundDifferentForSync) {
        cslog() << "NODE> reject transaction: sequence " << sequence << " is not actual, round " << round;
        return false;
    }
    return true;
}

void Node::clearRPCache(cs::RoundNumber rNum) {
    bool flagg = true;
    if (rNum < 6) {
        return;
    }
    while (flagg) {
        auto tmp = std::find_if(roundPackageCache_.begin(), roundPackageCache_.end(), [rNum](cs::RoundPackage& rp) {return rp.roundTable().round <= rNum - 5; });
        if (tmp == roundPackageCache_.end()) {
            break;
        }
        roundPackageCache_.erase(tmp);
    }

}

void Node::sendHash(cs::RoundNumber round) {
    if (!canBeTrusted(subRound_ != 0 /*critical, all trusted capable required*/)) {
        return;
    }

    if (blockChain_.getLastSeq() != round - 1) {
        // should not send hash until have got proper block sequence
        return;
    }

    csdebug() << "NODE> Sending hash to ALL";
    if (solver_->isInGrayList(solver_->getPublicKey())) {
        csinfo() << "NODE> In current Consensus " << cs::Conveyer::instance().confidantsCount()
            << " nodes will not propose this Node as Trusted Candidate. The probability to become Trusted is too low";
    }
    cs::Bytes message;
    cs::ODataStream stream(message);
    cs::Byte myTrustedSize = 0;
    cs::Byte myRealTrustedSize = 0;

    uint64_t lastTimeStamp = 0;
    uint64_t currentTimeStamp = 0;

    try {
        std::string lTS = getBlockChain().getLastTimeStamp();
        lastTimeStamp = std::stoull(lTS.empty() == 0 ? "0" : lTS);
        currentTimeStamp = cs::Utils::currentTimestamp(); // nothrow itself but may be skipped due to prev calls, keep this logic
    }
    catch (const std::exception& exception) {
        cswarning() << exception.what();
    }

    if (currentTimeStamp < lastTimeStamp) {
        currentTimeStamp = lastTimeStamp + 1;
    }

    csdebug() << "TimeStamp = " << std::to_string(currentTimeStamp);

    if (cs::Conveyer::instance().currentRoundNumber() > 1) {
        cs::Bytes lastTrusted = getBlockChain().getLastRealTrusted();
        myTrustedSize = static_cast<uint8_t>(lastTrusted.size());
        myRealTrustedSize = cs::TrustedMask::trustedSize(lastTrusted);
    }

    csdb::PoolHash tmp = spoileHash(blockChain_.getLastHash(), solver_->getPublicKey());
    stream << tmp.to_binary() << myTrustedSize << myRealTrustedSize << currentTimeStamp << round << subRound_;

    cs::Signature signature = cscrypto::generateSignature(solver_->getPrivateKey(), message.data(), message.size());
    cs::Bytes messageToSend(message.data(), message.data() + message.size() - sizeof(cs::RoundNumber) - sizeof(cs::Byte));

    // try to send to confidants..
    const auto& confidants = cs::Conveyer::instance().confidants();

    sendDirect(confidants, MsgTypes::BlockHash, round, subRound_, messageToSend, signature);
    csdebug() << "NODE> Hash sent, round: " << round << "." << cs::numeric_cast<int>(subRound_) << ", message: " << cs::Utils::byteStreamToHex(messageToSend);
}

void Node::getHash(const uint8_t* data, const size_t size, cs::RoundNumber rNum, const cs::PublicKey& sender) {
    if (myLevel_ != Level::Confidant) {
        csdebug() << "NODE> ignore hash as no confidant";
        return;
    }

    csdetails() << "NODE> get hash of round " << rNum << ", data size " << size;

    cs::IDataStream stream(data, size);
    uint8_t subRound = 0;
    stream >> subRound;

    if (subRound > subRound_) {
        cswarning() << "NODE> We got hash for the Node with SUBROUND: " << static_cast<int>(subRound) << " required #" << static_cast<int>(subRound_);
        // We don't have to return, the has of previous is the same 
    }

    cs::Bytes message;
    cs::Signature signature;
    stream >> message >> signature;

    if (!stream.isValid() || !stream.isEmpty()) {
        cswarning() << "NODE> bad hash packet format";
        return;
    }

    cs::StageHash sHash;
    cs::Bytes tmp;
    sHash.sender = sender;
    sHash.round = rNum;

    cs::IDataStream hashStream(message.data(), message.size());
    hashStream >> tmp;
    hashStream >> sHash.trustedSize;
    hashStream >> sHash.realTrustedSize;
    hashStream >> sHash.timeStamp;

    if (!hashStream.isEmpty() || !hashStream.isValid()) {
        csdebug() << "Stream is a bit uncertain ... ";
    }

    uint64_t lastTimeStamp = 0;
    uint64_t currentTimeStamp = 0;

    try {
        std::string lTS = getBlockChain().getLastTimeStamp();
        lastTimeStamp = std::stoull(lTS.empty() == 0 ? "0" : lTS);
        currentTimeStamp = cs::Utils::currentTimestamp(); // nothrow, may be skipped by prev calls, so keep this logic anyway
    }
    catch (const std::exception& exception) {
        cswarning() << exception.what();
    }

    csdebug() << "NODE> GetHash - TimeStamp     = " << std::to_string(sHash.timeStamp);
    uint64_t deltaStamp = currentTimeStamp - lastTimeStamp;
    if (deltaStamp > Consensus::DefaultTimeStampRange) {
        deltaStamp = Consensus::DefaultTimeStampRange;

    }
    if (deltaStamp < Consensus::MinimumTimeStampRange) {
        deltaStamp = Consensus::MinimumTimeStampRange;

    }
    if (sHash.timeStamp < lastTimeStamp){
        csdebug() << "Incoming TimeStamp(< last BC timeStamp)= " << std::to_string(sHash.timeStamp) << " < " << std::to_string(lastTimeStamp) << " ... return";
        return;
    }

    if (sHash.timeStamp > currentTimeStamp + deltaStamp / 2 * 3) {//here we just take the time interval 1.5 times larger than last round
        csdebug() << "Incoming TimeStamp(> current timeStamp + delta) = " << std::to_string(sHash.timeStamp) << " > " << std::to_string(currentTimeStamp) << " ... return";
        return;
    }

    sHash.hash = csdb::PoolHash::from_binary(std::move(tmp));
    cs::ODataStream stream1(message);
    stream1 << rNum << subRound;

    if (!cscrypto::verifySignature(signature, sender, message.data(), message.size())) {
        csdebug() << "Hash message signature is NOT VALID";
        return;

    }

    csdebug() << "Hash message signature is  VALID";
    csdebug() << "Got Hash message (" << tmp.size() << "): " << cs::Utils::byteStreamToHex(tmp.data(), tmp.size())
        << " : " << static_cast<int>(sHash.trustedSize) << " - " << static_cast<int>(sHash.realTrustedSize);

    uint8_t myRealTrustedSize = 0;

    if (cs::Conveyer::instance().currentRoundNumber() > 1) {
        cs::Bytes lastTrusted = getBlockChain().getLastRealTrusted();
        myRealTrustedSize = cs::TrustedMask::trustedSize(lastTrusted);
    }

    solver_->gotHash(std::move(sHash), myRealTrustedSize);
}

void Node::roundPackRequest(const cs::PublicKey& respondent, cs::RoundNumber round) {
    csdebug() << "NODE> send request for round info #" << round;
    sendDirect(respondent, MsgTypes::RoundPackRequest, round);
}

void Node::askConfidantsRound(cs::RoundNumber round, const cs::ConfidantsKeys& confidants) {
    csdebug() << "NODE> ask round info #" << round << " from confidants";

    if (confidants.empty()) {
        return;
    }

    sendDirect(confidants, MsgTypes::RoundPackRequest, round);
    cslog() << "NODE> unable to request round info #" << round;
}

void Node::getRoundPackRequest(const uint8_t* data, const size_t size, cs::RoundNumber rNum, const cs::PublicKey& sender) {
    csunused(data);
    csunused(size);

    csdebug() << "NODE> getting roundPack request #" << rNum;

    if (roundPackageCache_.size() == 0) {
        csdebug() << "NODE> can't send = don't have last RoundPackage filled";
        return;
    }

    cs::RoundPackage& roundPackage = roundPackageCache_.back();
    const auto& table = roundPackage.roundTable();

    if (table.round >= rNum) {
        if (!roundPackage.roundSignatures().empty()) {
            auto iter = std::find(std::cbegin(table.confidants), std::cend(table.confidants), sender);

            if (iter != table.confidants.cend()) {
                ++roundPackRequests_;
            }

            if (roundPackRequests_ > table.confidants.size() / 2 && roundPackRequests_ <= table.confidants.size() / 2 + 1) {
                sendRoundPackageToAll(roundPackage);
            }
            else {
                roundPackReply(sender);
            }
        }
        else {
            emptyRoundPackReply(sender);
        }
    }
}

void Node::emptyRoundPackReply(const cs::PublicKey& respondent) {
    csdebug() << "NODE> sending empty roundPack reply to " << cs::Utils::byteStreamToHex(respondent.data(), respondent.size());
    cs::Sequence seq = getBlockChain().getLastSeq();
    cs::Bytes bytes;
    cs::ODataStream stream(bytes);
    stream << seq;
    cs::Signature signature = cscrypto::generateSignature(solver_->getPrivateKey(), bytes.data(), bytes.size());
    sendDirect(respondent, MsgTypes::EmptyRoundPack, seq, signature);
}

void Node::getEmptyRoundPack(const uint8_t* data, const size_t size, cs::RoundNumber rNum, const cs::PublicKey& sender) {
    csdebug() << "NODE> get empty roundPack reply from " << cs::Utils::byteStreamToHex(sender.data(), sender.size());

    cs::IDataStream stream(data, size);

    cs::Signature signature;
    stream >> signature;

    cs::Bytes bytes;
    cs::ODataStream message(bytes);
    message << rNum;

    if (rNum <= getBlockChain().getLastSeq()) {
        return;
    }

    if (!cscrypto::verifySignature(signature, sender, bytes.data(), bytes.size())) {
        csdebug() << "NODE> the RoundPackReply signature is not correct";
        return;
    }

    cs::Conveyer::instance().setRound(rNum + 1); // There are no rounds at all on remote, "Round" = LastSequence(=rNum) + 1
    processSync();
}

void Node::roundPackReply(const cs::PublicKey& respondent) {
    csdebug() << "NODE> sending roundPack reply to " << cs::Utils::byteStreamToHex(respondent.data(), respondent.size());

    if (roundPackageCache_.size() == 0) {
        csdebug() << "NODE> can't send = don't have last RoundPackage filled";
        return;
    }

    cs::RoundPackage rp = roundPackageCache_.back();
    bool showVersion = rp.roundTable().round >= Consensus::StartingDPOS && Consensus::miningOn;
    sendDirect(respondent, MsgTypes::RoundTable, rp.roundTable().round, rp.subRound(), rp.toBinary(showVersion));
}

void Node::sendRoundTableRequest(uint8_t respondent) {
    // ask for round info from current trusted on current round
    std::optional<cs::PublicKey> confidant = cs::Conveyer::instance().confidantIfExists(respondent);

    if (confidant.has_value()) {
        sendRoundTableRequest(confidant.value());
    }
    else {
        cserror() << "NODE> cannot request round info, incorrect respondent number";
    }
}

void Node::sendRoundTableRequest(const cs::PublicKey& respondent) {
    const auto round = cs::Conveyer::instance().currentRoundNumber();
    csdebug() << "NODE> send request for next round info after #" << round;

    // ask for next round info:
    sendDirect(respondent, MsgTypes::RoundTableRequest, round, myConfidantIndex_);
}

void Node::getRoundTableRequest(const uint8_t* data, const size_t size, const cs::RoundNumber rNum, const cs::PublicKey& requester) {
    csmeta(csdetails) << "started, round: " << rNum;

    cs::IDataStream stream(data, size);

    uint8_t requesterNumber;
    stream >> requesterNumber;

    if (!stream.isValid() || !stream.isEmpty()) {
        cserror() << "NODE> bad RoundInfo request packet format";
        return;
    }

    // special request to re-send again handling
    if (requesterNumber >= cs::Conveyer::instance().confidantsCount()) {
        cserror() << "NODE> incorrect T[" << cs::numeric_cast<int>(requesterNumber) << "] asks for round table";
        return;
    }

    // default request from other trusted node handling
    csdebug() << "NODE> get request for next round info after #" << rNum << " from T[" << cs::numeric_cast<int>(requesterNumber) << "]";
    solver_->gotRoundInfoRequest(requester, rNum);
}

void Node::sendRoundTableReply(const cs::PublicKey& target, bool hasRequestedInfo) {
    csdebug() << "NODE> send RoundInfo reply to " << getSenderText(target);

    if (myLevel_ != Level::Confidant) {
        csdebug() << "Only confidant nodes can reply consensus stages";
    }

    sendDirect(target, MsgTypes::RoundTableReply, cs::Conveyer::instance().currentRoundNumber(), hasRequestedInfo);
}

bool Node::tryResendRoundTable(const cs::PublicKey& target, const cs::RoundNumber rNum) {
    if (lastSentRoundData_.table.round != rNum || lastSentRoundData_.subRound != subRound_) {
        csdebug() << "NODE> unable to repeat round data #" << rNum;
        return false;
    }

    csdebug() << "NODE> Re-send last round info #" << rNum << " to " << cs::Utils::byteStreamToHex(target.data(), target.size());
    auto rPackage = std::find_if(roundPackageCache_.begin(), roundPackageCache_.end(), [rNum] (cs::RoundPackage& rp) {return rp.roundTable().round == rNum;});

    if (rPackage == roundPackageCache_.cend()) {
        return false;
    }

    return sendRoundPackage(rNum, target);
}

void Node::getRoundTableReply(const uint8_t* data, const size_t size, const cs::PublicKey& respondent) {
    csmeta(csdetails);

    if (myLevel_ != Level::Confidant) {
        return;
    }

    cs::IDataStream stream(data, size);

    bool hasRequestedInfo;
    stream >> hasRequestedInfo;

    if (!stream.isValid() || !stream.isEmpty()) {
        csdebug() << "NODE> bad RoundInfo reply packet format";
        return;
    }

    solver_->gotRoundInfoReply(hasRequestedInfo, respondent);
}

std::string Node::KeyToBase58(cs::PublicKey key) {
    const auto beg = key.data();
    const auto end = beg + key.size();
    return EncodeBase58(beg, end);
}

void Node::onRoundStart(const cs::RoundTable& roundTable, bool updateRound) {
    bool found = false;
    uint8_t confidantIndex = 0;

    for (auto& conf : roundTable.confidants) {
        if (conf == nodeIdKey_) {
            myLevel_ = Level::Confidant;
            myConfidantIndex_ = confidantIndex;
            found = true;
            break;
        }

        confidantIndex++;
    }

    if (!found) {
        myLevel_ = Level::Normal;
        myConfidantIndex_ = cs::ConfidantConsts::InvalidConfidantIndex;
        if (stopRequested_) {
            stop();
            return;
        }
    }

    updateBlackListCounter();
    // TODO: think how to improve this code.
    stageOneMessage_.clear();
    stageOneMessage_.resize(roundTable.confidants.size());
    stageTwoMessage_.clear();
    stageTwoMessage_.resize(roundTable.confidants.size());
    stageThreeMessage_.clear();
    stageThreeMessage_.resize(roundTable.confidants.size());
    stageThreeSent_ = false;
    roundPackRequests_ = 0;
    lastBlockRemoved_ = false;
    kLogPrefix_ = "R-" + std::to_string(roundTable.round) + " NODE> ";
    constexpr int padWidth = 30;

    badHashReplyCounter_.clear();
    badHashReplyCounter_.resize(roundTable.confidants.size());

    auto checkOrdered = orderedPackets_.begin();
    while (checkOrdered != orderedPackets_.end()) {
        if (checkOrdered->second + cs::ConfigHolder::instance().config()->conveyerData().maxPacketLifeTime < roundTable.round) {
            checkOrdered = orderedPackets_.erase(checkOrdered);
        }
        else {
            ++checkOrdered;
        }
    }


    for (auto badHash : badHashReplyCounter_) {
        badHash = false;
    }

    std::ostringstream line1;
    for (int i = 0; i < padWidth; i++) {
        line1 << '=';
    }

    line1 << " R-" << WithDelimiters(cs::Conveyer::instance().currentRoundNumber()) << "." << cs::numeric_cast<int>(subRound_) << " ";

    if (Level::Normal == myLevel_) {
        line1 << "NORMAL";
        if (getBlockChain().getLastSeq() + 1ULL == cs::Conveyer::instance().currentRoundNumber()) {
            status_ = cs::NodeStatus::InRound;
        }
        else {
            status_ = cs::NodeStatus::Synchronization;
        }
    }
    else {
        line1 << "TRUSTED [" << cs::numeric_cast<int>(myConfidantIndex_) << "]";
        status_ = cs::NodeStatus::Trusted;
    }

    line1 << ' ';

    for (int i = 0; i < padWidth; i++) {
        line1 << '=';
    }

    const auto s = line1.str();
    const std::size_t fixedWidth = s.size();

    cslog() << s;
    csdebug() << " Node key " << KeyToBase58(nodeIdKey_);
    cslog() << " Last written sequence = " << WithDelimiters(blockChain_.getLastSeq()) << ", neighbour nodes = " << transport_->getNeighboursCount();

    std::ostringstream line2;

    for (std::size_t i = 0; i < fixedWidth; ++i) {
        line2 << '-';
    }

    csdebug() << line2.str();
    csdebug() << " Confidants:";
    for (size_t i = 0; i < roundTable.confidants.size(); ++i) {
        auto result = myLevel_ == Level::Confidant && i == myConfidantIndex_;
        auto name = result ? "me" : KeyToBase58(roundTable.confidants[i]);

        csdebug() << "[" << i << "] " << name;
    }

    csdebug() << " Hashes: " << roundTable.hashes.size();
    for (size_t j = 0; j < roundTable.hashes.size(); ++j) {
        csdetails() << "[" << j << "] " << cs::Utils::byteStreamToHex(roundTable.hashes[j].toBinary());
    }

    if (roundTable.hashes.empty()) {
        cslog() << " Trusted count: " << roundTable.confidants.size() << ", no transactions";
    }
    else {
        cslog() << " Trusted count: " << roundTable.confidants.size() << ", transaction packets: " << roundTable.hashes.size();
    }

    csdebug() << line2.str();
    stat_.onRoundStart(cs::Conveyer::instance().currentRoundNumber(), false /*skip_logs*/);
    csdebug() << line2.str();

    solver_->nextRound(updateRound);
    if (cacheLBs_) {
        getBlockChain().cacheLastBlocks();
        if (getBlockChain().getIncorrectBlockNumbers()->empty()) {
            cacheLBs_ = false;
        }
    }
    if (!sendingTimer_.isRunning()) {
        csdebug() << "NODE> Transaction timer started";
        sendingTimer_.start(cs::TransactionsPacketInterval);
    }
}

void Node::startConsensus() {
    cs::RoundNumber roundNumber = cs::Conveyer::instance().currentRoundNumber();
    solver_->gotConveyerSync(roundNumber);
    transport_->processPostponed(roundNumber);

    // claim the trusted role only if have got proper blockchain:
    if (roundNumber == blockChain_.getLastSeq() + 1) {
        sendHash(roundNumber);
    }
}

std::string Node::getSenderText(const cs::PublicKey& sender) {
    std::ostringstream os;
    unsigned idx = 0;

    for (const auto& key : cs::Conveyer::instance().confidants()) {
        if (std::equal(key.cbegin(), key.cend(), sender.cbegin())) {
            os << "T[" << idx << "]";
            return os.str();
        }

        ++idx;
    }

    os << "N (" << cs::Utils::byteStreamToHex(sender.data(), sender.size()) << ")";
    return os.str();
}

csdb::PoolHash Node::spoileHash(const csdb::PoolHash& hashToSpoil) {
    const auto& binary = hashToSpoil.to_binary();
    const auto round = cs::Conveyer::instance().currentRoundNumber();
    cs::Hash hash = cscrypto::calculateHash(binary.data(), binary.size(), reinterpret_cast<cs::Byte*>(round), sizeof(round));
    cs::Bytes bytesHash(hash.begin(), hash.end());

    return csdb::PoolHash::from_binary(std::move(bytesHash));
}

csdb::PoolHash Node::spoileHash(const csdb::PoolHash& hashToSpoil, const cs::PublicKey& pKey) {
    const auto& binary = hashToSpoil.to_binary();
    cs::Hash hash = cscrypto::calculateHash(binary.data(), binary.size(), pKey.data(), pKey.size());
    cs::Bytes bytesHash(hash.begin(), hash.end());

    return csdb::PoolHash::from_binary(std::move(bytesHash));
}

void Node::smartStageEmptyReply(uint8_t requesterNumber) {
    csunused(requesterNumber);
    csdebug() << "Here should be the smart refusal for the SmartStageRequest";
}

void Node::sendHashReply(const csdb::PoolHash& hash, const cs::PublicKey& respondent) {
    csmeta(csdebug);
    if (myLevel_ != Level::Confidant) {
        csmeta(csdebug) << "Only confidant nodes can send hash reply to other nodes";
        return;
    }

    cs::Signature signature = cscrypto::generateSignature(solver_->getPrivateKey(), hash.to_binary().data(), hash.size());
    /*bool notused =*/ sendDirect(respondent, MsgTypes::HashReply, cs::Conveyer::instance().currentRoundNumber(), subRound_, signature, getConfidantNumber(), hash);//add block sequence and block weight
}

void Node::getHashReply(const uint8_t* data, const size_t size, cs::RoundNumber rNum, const cs::PublicKey& sender) {
    if (myLevel_ == Level::Confidant) {
        csmeta(csdebug) << "I'm confidant. Exit from getHashReply";
        return;
    }

    csmeta(csdebug);

    cs::IDataStream stream(data, size);
    uint8_t subRound = 0;
    stream >> subRound;

    const auto& conveyer = cs::Conveyer::instance();

    if (conveyer.currentRoundNumber() != rNum || subRound_ != subRound) {
        csdebug() << "NODE> Get hash reply on incorrect round: " << rNum << "(" << subRound << ")";
        return;
    }

    cs::Signature signature;
    stream >> signature;

    uint8_t senderNumber = 0;
    stream >> senderNumber;

    csdb::PoolHash hash;
    stream >> hash;

    if (!conveyer.isConfidantExists(senderNumber)) {
        csmeta(csdebug) << "The message of WRONG HASH was sent by false confidant!";
        return;
    }

    if (badHashReplyCounter_[senderNumber]) {
        csmeta(csdetails) << "Sender num: " << senderNumber << " already send hash reply";
        return;
    }

    badHashReplyCounter_[senderNumber] = true;

    if (!cscrypto::verifySignature(signature, sender, hash.to_binary().data(), hash.size())) {
        csmeta(csdebug) << "The message of WRONG HASH has WRONG SIGNATURE!";
        return;
    }

    const auto badHashReplySummary = std::count_if(badHashReplyCounter_.begin(), badHashReplyCounter_.end(), [](bool badHash) { return badHash; });

    if ((static_cast<size_t>(badHashReplySummary) > conveyer.confidantsCount() / 2) || (isBootstrapRound_ && (static_cast<size_t>(badHashReplySummary) > 1)) && !lastBlockRemoved_) {
        csmeta(csdebug) << "This node really have not valid HASH!!! Removing last block from DB and trying to syncronize";
        // TODO: examine what will be done without this function
        if (!roundPackageCache_.empty() && roundPackageCache_.back().poolMetaInfo().realTrustedMask.size() > cs::TrustedMask::trustedSize(roundPackageCache_.back().poolMetaInfo().realTrustedMask)) {
            blockChain_.setBlocksToBeRemoved(1U);
            if (!blockChain_.compromiseLastBlock(hash)) {
                blockChain_.removeLastBlock();
            }
            lastBlockRemoved_ = true;
        }

    }
}

/*static*/
void Node::requestStop() {
    // use existing global flag as a crutch against duplicated request handling
    if (gSignalStatus == 0) {
        return;
    }
    gSignalStatus = 0;
    emit stopRequested();
}

void Node::onStopRequested() {
    if (stopRequested_) {
        stop();
        return;
    }

    stopRequested_ = true;

    if (myLevel_ == Level::Confidant) {
        cslog() << "Node: wait until complete trusted role before exit";
        blockChain_.tryFlushDeferredBlock();
    }
    else {
        stop();
    }

    cs::Executor::instance().stop();
    cswarning() << "[EXECUTOR IS SIGNALED TO STOP]";
}


bool Node::checkNodeVersion(cs::Sequence curSequence, std::string& msg) {
    if (nVersionChange_.check == cs::CheckVersion::None) {
        nVersionChange_.condition = false;
        return true;
    }
    if (nVersionChange_.check == cs::CheckVersion::Full) {
        msg =  "THIS NODE VERSION " + std::to_string(NODE_VERSION) + " IS OBSOLETTE AND IS TOTALLY NOT COMPATIBLE TO NEW NODE VERSION "
            + std::to_string(nVersionChange_.minFullVersion) + ".\nSINCE POOL "
            + std::to_string(nVersionChange_.seq) + " THIS NODE WILL NOT WORK. \nPLEASE UPDATE YOUR SOFTWARE!";
        if (curSequence >= nVersionChange_.seq) {
            cslog() << msg;
            this->stop();
        }
        return !nVersionChange_.condition;
    }
    if (nVersionChange_.check == cs::CheckVersion::Normal) {
        msg =  "THIS NODE VERSION " + std::to_string(NODE_VERSION) + " IS OBSOLETTE AND IS NOT FULLY COMPATIBLE TO NEW NODE VERSION " 
            + std::to_string(nVersionChange_.minFullVersion) + ".\nSINCE POOL "
            + std::to_string(nVersionChange_.seq) + " THIS NODE WILL NOT WORK IN CONSENSUS. \nPLEASE UPDATE YOUR SOFTWARE!";
        if (curSequence >= nVersionChange_.seq) {
            nVersionChange_.condition = true;
        }
        //nVersionChange_.condition = nVersionChange_.condition || nVersionChange_.seq < curSequence;
    }
    //nVersionChange_.condition = nVersionChange_.condition && (NODE_VERSION >= nVersionChange_.minFullVersion || nVersionChange_.seq < curSequence);
    return !nVersionChange_.condition;
}

//void Node::restoreSequence(cs::Sequence seq) {
//     //TODO: insert necessary code here
//}

void Node::processSpecialInfo(const csdb::Pool& pool) {
    for (auto it : pool.transactions()) {
        if (!getBlockChain().isSpecial(it)) {
            continue;
        } 
        else {
            auto stringBytes = it.user_field(cs::trx_uf::sp::managing).value<std::string>();
            std::vector<cs::Byte> msg(stringBytes.begin(), stringBytes.end());
            cs::IDataStream stream(msg.data(), msg.size());
            uint16_t order;
            stream >> order;

            if (order == 2U) {
                uint8_t cnt;
                stream >> cnt;
                if (size_t(cnt) < Consensus::MinTrustedNodes) {
                  continue;
                }
                cslog() << "New bootstrap nodes: ";
                initialConfidants_.clear();
                for (uint8_t i = 0; i < cnt; ++i) {
                    cs::PublicKey key;
                    stream >> key;
                    initialConfidants_.insert(key);
                    cslog() << static_cast<int>(i) << ". " << cs::Utils::byteStreamToHex(key);
                }

                if (initialConfidants_.find(solver_->getPublicKey()) != initialConfidants_.end()) {
                    transport_->setPermanentNeighbours(initialConfidants_);
                }
            }

            if (order == 5U) {
                /*current ver < minCompatibleVersion                    - node stop working, 
                minCompatibleVersion <= current ver  < minFullVersion   - node works only as normal,  
                minFullVersion <= current ver                           - node workds with full functionality*/
                stream >> nVersionChange_.seq >> nVersionChange_.minFullVersion >> nVersionChange_.minCompatibleVersion;
                nVersionChange_.check = NODE_VERSION >= nVersionChange_.minFullVersion ? cs::CheckVersion::None : (NODE_VERSION < nVersionChange_.minCompatibleVersion ? cs::CheckVersion::Full : cs::CheckVersion::Normal);
            }

            if (order == 9U) {// max round mum smart contract execution time
                cs::Sequence startDPOSSequence;
                stream >> startDPOSSequence;
                Consensus::StartingDPOS = startDPOSSequence;
                cslog() << "StartingDPOS sequrnce changed to: " << Consensus::StartingDPOS;
            }

            if (order == 10U) {// transaction's packages life time

            }

            if (order == 11U) {// max round mum smart contract execution time
                unsigned int maxContractExeTime;
                stream >> maxContractExeTime;
                Consensus::MaxRoundsExecuteContract = maxContractExeTime;
                cslog() << "MaxRoundsExecuteContract changed to: " << Consensus::MaxRoundsExecuteContract;
            }

            if (order == 21U) {// Stage One maximum size set
                uint64_t value;
                stream >> value;
                Consensus::StageOneMaximumSize = value;
                cslog() << "StageOneMaximumSize changed to: " << Consensus::StageOneMaximumSize;
            }

            if (order == 22U) {// Minimum stake size set
                int32_t integral;
                uint64_t fraction;;
                stream >> integral >> fraction;
                Consensus::MinStakeValue = csdb::Amount(integral,fraction);
                cslog() << "MinStakeValue changed to: " << Consensus::MinStakeValue.to_string();
            }

            if (order == 23U) {// Stage One hashes collecting time set
                uint32_t value;
                stream >> value;
                Consensus::TimeMinStage1 = value;
                cslog() << "TimeMinStage1 changed to: " << Consensus::TimeMinStage1;
                saveConsensusSettingsToChain();
            }

            if (order == 24U) {// Gray list punishment set
                uint32_t value;
                stream >> value;
                Consensus::GrayListPunishment = value;
                cslog() << "GrayListPunishment changed to: " << Consensus::GrayListPunishment;
            }

            if (order == 25U) {// Stage One maximum hashes number set
                uint64_t value;
                stream >> value;
                Consensus::MaxStageOneHashes = value;
                cslog() << "MaxStageOneHashes changed to: " << Consensus::MaxStageOneHashes;
            }

            if (order == 26U) {// Transaction max size set
                uint64_t value;
                stream >> value;
                Consensus::MaxTransactionSize = value;
                cslog() << "MaxTransactionSize changed to: " << Consensus::MaxTransactionSize;
            }

            if (order == 27U) {// Stage One hashes maximum number set
                uint64_t value;
                stream >> value;
                Consensus::MaxStageOneTransactions = value;
                cslog() << "MaxStageOneTransactions changed to: " << Consensus::MaxStageOneTransactions;
            }

            if (order == 28U) {// Stage One block maximum estimation size set
                uint64_t value;
                stream >> value;
                Consensus::MaxPreliminaryBlockSize = value;
                cslog() << "MaxPreliminaryBlockSize changed to: " << Consensus::MaxPreliminaryBlockSize;
            }

            if (order == 29U) {// API accepts MaxPacketsPerRound
                uint64_t value;
                stream >> value;
                Consensus::MaxPacketsPerRound = value;
                cslog() << "MaxPacketsPerRound changed to: " << Consensus::MaxPacketsPerRound;
            }

            if (order == 30U) {// MaxPacketTransactions in one Conveyer packet
                uint64_t value;
                stream >> value;
                Consensus::MaxPacketTransactions = value;
                cslog() << "MaxPacketTransactions changed to: " << Consensus::MaxPacketTransactions;
            }

            if (order == 31U) {// MaxQueueSize - if full no transactions
                uint64_t value;
                stream >> value;
                Consensus::MaxQueueSize = value;
                cslog() << "MaxQueueSize changed to: " << Consensus::MaxQueueSize;
            }

            if (order == 32U) {
                uint8_t cnt;
                stream >> cnt;
                csdebug() << "Blacklisted smart-contracts: ";
                for (uint8_t i = 1; i <= cnt; ++i) {
                    cs::PublicKey key;
                    stream >> key;
                    csdb::Address addr = csdb::Address::from_public_key(key);
                    solver_->smart_contracts().setBlacklisted(addr, true);
                    cslog() << static_cast<int>(i) << ". " << cs::Utils::byteStreamToHex(key);
                }
            }

            if (order == 33U) {
                uint8_t cnt;
                stream >> cnt;
                csdebug() << "Rehabilitated smart-contracts: ";
                for (uint8_t i = 1; i <= cnt; ++i) {
                    cs::PublicKey key;
                    stream >> key;
                    csdb::Address addr = csdb::Address::from_public_key(key);
                    solver_->smart_contracts().setBlacklisted(addr, false);
                    cslog() << static_cast<int>(i) << ". " << cs::Utils::byteStreamToHex(key);
                }
            }

            if (order == 35U) {// apply new global features
                uint64_t value;
                stream >> value;
                Consensus::syncroChangeRound = value;
                cslog() << "Changes will be aplied in round " << Consensus::syncroChangeRound;
            }

            if (order == 37U) {// turn on/off mining
                uint8_t sign;
                uint64_t round;
                int32_t rewInt;
                int32_t coeffInt;
                uint64_t rewFrac;
                uint64_t coefFrac;
                stream >> sign >> round >> rewInt >> rewFrac >> coeffInt >> coefFrac;
                
                if (round == 0ULL || round == ULLONG_MAX) {
                    consensusSettingsChangingRound_ = ULLONG_MAX;
                }
                consensusSettingsChangingRound_ = round;
                stakingOn_ = (sign == 3 || sign == 2) ? true : false;
                miningOn_ = (sign == 3 || sign == 1) ? true : false;
                blockReward_ = csdb::Amount(rewInt, rewFrac);
                miningCoefficient_ = csdb::Amount(coeffInt, coefFrac);

                cslog() << "Mining settings will be changed in round " << consensusSettingsChangingRound_ << ": \n staking " << (stakingOn_ ? "ON" : "OFF")
                    << "\n mining " << (miningOn_ ? "ON" : "OFF")
                    << "\n blockReward " << blockReward_.to_string()
                    << "\n miningCoefficient " << miningCoefficient_.to_string();
            }

        }
    }
    std::string msg;
    checkNodeVersion(pool.sequence(), msg);
    checkConsensusSettings(pool.sequence(), msg);
    if (!msg.empty()) {
        cslog() << msg;
    }
}

void Node::saveConsensusSettingsToChain() {
    blockChain_.setBlockReward(Consensus::blockReward);
    blockChain_.setMiningCoefficient(Consensus::miningCoefficient);
    blockChain_.setMiningOn(Consensus::miningOn);
    blockChain_.setStakingOn(Consensus::stakingOn);
    blockChain_.setTimeMinStage1(Consensus::TimeMinStage1);
}

void Node::checkConsensusSettings(cs::Sequence seq, std::string& msg){
    if (seq != consensusSettingsChangingRound_) {
        return;
    }
    consensusSettingsChangingRound_ = ULLONG_MAX;
    Consensus::stakingOn = stakingOn_;
    Consensus::miningOn = miningOn_;
    Consensus::blockReward = blockReward_;
    Consensus::miningCoefficient = miningCoefficient_;
    bool msgIs = msg.size() > 0;
    std::string miningStr = Consensus::miningOn ? "true" : "false";
    std::string stakingStr = Consensus::stakingOn ? "true" : "false";
    std::string curMsg = "Changing consensus settings to:\nstakingOn = " + stakingStr
        + "\nminingOn = " + miningStr
        + "\nblockReward = " + Consensus::blockReward.to_string()
        + "\nminingCoefficient = " + Consensus::miningCoefficient.to_string();
    msg += (msg.size() > 0) ? "\n" + curMsg : curMsg;
    saveConsensusSettingsToChain();
}

void Node::validateBlock(const csdb::Pool& block, bool* shouldStop) {
    if (stopRequested_) {
        *shouldStop = true;
        return;
    }
    if (!blockValidator_->validateBlock(block,
        cs::BlockValidator::ValidationLevel::hashIntergrity 
            | cs::BlockValidator::ValidationLevel::blockNum
            /*| cs::BlockValidator::ValidationLevel::smartStates*/
            /*| cs::BlockValidator::ValidationLevel::accountBalance*/,
        cs::BlockValidator::SeverityLevel::greaterThanWarnings)) {
        *shouldStop = true;
        csdebug() << "NODE> Trying to add sequence " << block.sequence() << " to incorrect blocks list. NodeStatus: " 
            << (status_ == cs::NodeStatus::ReadingBlocks ? "ReadingBlocks" : "Other");
        if (status_ == cs::NodeStatus::ReadingBlocks) {
            getBlockChain().addIncorrectBlockNumber(block.sequence());
            csdebug() << "NODE> Sequence " << block.sequence() << " added";
            *shouldStop = false;
        }
        else {
            return;
        }
    }
    processSpecialInfo(block);
}


bool Node::checkKnownIssues(cs::Sequence seq) {
    constexpr const uint64_t uuidTestNet = 5283967947175248524ull;
    constexpr const uint64_t uuidMainNet = 11024959585341937636ull;
    /*constexpr*/static const std::vector<cs::Sequence> knownIssues = {49808948ULL, 49809401ULL, 49810282ULL, 49811537ULL, 49811641ULL
        , 49811642ULL, 49811837ULL, 49812510ULL, 49812630ULL, 49813324ULL, 49800595ULL, 49801727ULL, 49800231ULL, 49796878ULL, 53885714ULL
        , 553134820ULL , 55764724ULL, 56100940ULL, 49813512ULL, 49813681ULL, 49813786ULL, 49813838ULL, 49813974ULL, 49814174ULL, 49814445ULL
        , 49814815ULL, 49905285ULL, 50018546ULL, 50018547ULL, 50018837ULL, 50019054ULL, 50019055ULL, 50197075ULL, 50197157ULL, 50198765ULL
        , 50199232ULL, 50207040ULL, 50220444ULL, 50222051ULL, 50222361ULL, 50226227ULL, 50272824ULL, 50272929ULL, 50273609ULL, 50273928ULL
        , 50274075ULL, 50274976ULL, 50275076ULL, 50275216ULL, 50277189ULL, 50316661ULL, 50316672ULL, 50317469ULL, 50375092ULL, 50277303ULL
        , 50379862ULL, 50381484ULL, 50383224ULL, 50664307ULL, 55313482ULL, 55316412ULL, 55767834ULL };

    if (getBlockChain().uuid() == uuidMainNet) {
        // valid blocks in all cases
        if (seq <= 49'780'000 || std::find(knownIssues.begin(), knownIssues.end(), seq) != knownIssues.end()) {
            return true;
        }
    }
    if (getBlockChain().uuid() == uuidTestNet) {
        // valid blocks in all cases
        if (seq <= 36'190'000) {
            return true;
        }
    }

    return false;// blocks should be valitated


}

void Node::deepBlockValidation(const csdb::Pool& block, bool* check_failed) {//check_failed should be FALSE of the block is ok 
    *check_failed = false;
    const auto seq = block.sequence();
    if (seq == 0) {
        return;
    }
    if (block.transactions_count() == 0) {
        return;
    }
    auto smartPacks = cs::SmartContracts::grepNewStatesPacks(getBlockChain(), block.transactions());
    auto& smartSignatures = block.smartSignatures();
    size_t smartTrxCounter = 0;
    /*constexpr*/ const bool collectRejectedInfo = cs::ConfigHolder::instance().config()->isCompatibleVersion();
    const char* kLogPrefix = (collectRejectedInfo ? "NODE> skip block validation: " : "NODE> stop block validation: ");

    if(checkKnownIssues(block.sequence())) {
        return;
    }

    if (smartPacks.size() != smartSignatures.size()) {
        // there was known accident in testnet only in block #2'651'597 that contains unsigned smart contract states packet
        //if (getBlockChain().uuid() == uuidTestNet) {
        cserror() << kLogPrefix << "different size of smartpackets and signatures in block " << WithDelimiters(block.sequence());
        *check_failed = !collectRejectedInfo;
        return;
    }

    csdebug() << "NODE> SmartPacks = " << smartPacks.size();
    auto iSignatures = smartSignatures.begin();
    for (auto& it : smartPacks) {
        //csdebug() << "NODE> SmartSignatures(" << iSignatures->signatures.size() << ") for contract "<< iSignatures->smartConsensusPool << ":";
        for (auto p : iSignatures->signatures) {
            it.addSignature(p.first, p.second);
            //csdebug() << "NODE> " << static_cast<int>(p.first) << ". " << cs::Utils::byteStreamToHex(p.second.data(), 64);
        }
        smartTrxCounter += it.transactionsCount();
        csdebug() << "NODE> setting exp Round = " << iSignatures->smartConsensusPool + Consensus::MaxRoundsCancelContract;
        it.setExpiredRound(iSignatures->smartConsensusPool + Consensus::MaxRoundsCancelContract);
        it.makeHash();
        ++iSignatures;
    }

    cs::TransactionsPacket trxs;
    int normalTrxCounter = 0;
    for (auto& it : block.transactions()) {
        if (it.signature() != cs::Zero::signature) {
            ++normalTrxCounter;
        }
        trxs.addTransaction(it);
    }
    if (normalTrxCounter + smartTrxCounter != block.transactions_count()) {
        cserror() << kLogPrefix << "invalid number of signed transactions in block " << WithDelimiters(block.sequence());
        *check_failed = !collectRejectedInfo;
        return;
    }
    auto characteristic = solver_->ownValidation(trxs, smartPacks);
    if (!characteristic.has_value()) {
        cserror() << kLogPrefix << "cannot get characteristic from block " << WithDelimiters(block.sequence());
        *check_failed = !collectRejectedInfo;
        return;
    }
    auto cMask = characteristic.value().mask;
    size_t idx = 0;
    for (auto it : cMask) {
        if (it == 0) {
            cserror() << kLogPrefix << "invalid transaction found " << WithDelimiters(block.sequence())
                << '.' << idx;
            *check_failed = !collectRejectedInfo;
            return;
        }
        ++idx;
    }
    csdebug() << "NODE> no invalid transactions in block #" <<WithDelimiters(block.sequence());
}

void Node::onRoundTimeElapsed() {
    solver_->resetGrayList();
    const cs::PublicKey& own_key = solver_->getPublicKey();
    if (initialConfidants_.find(own_key) == initialConfidants_.end()) {
        cslog() << "Waiting for next round...";

        myLevel_ = Level::Normal;
        myConfidantIndex_ = cs::ConfidantConsts::InvalidConfidantIndex;

        initBootstrapRP(initialConfidants_);

        // if we have correct last block, we pretend to next trusted role
        // otherwise remote nodes will drop our hash
        //sendHash(blockChain_.getLastSeq() + 1);
        return;
    }

    cslog() << "Gathering info to start round...";

    std::set<cs::PublicKey> actualConfidants;
    actualConfidants.insert(own_key);

    const cs::Sequence maxLocalBlock = blockChain_.getLastSeq();
    cs::Sequence maxGlobalBlock = maxLocalBlock;

    auto callback = [&maxGlobalBlock, &actualConfidants, this]
                    (const cs::PublicKey& neighbour, cs::Sequence lastSeq, cs::RoundNumber) {
                        const auto it = initialConfidants_.find(neighbour);
                        if (it == initialConfidants_.end()) {
                            return;
                        }
                        if (lastSeq > maxGlobalBlock) {
                            maxGlobalBlock = lastSeq;
                            actualConfidants.clear();
                            actualConfidants.insert(*it);
                        }
                        else if (lastSeq == maxGlobalBlock) {
                            actualConfidants.insert(*it);
                        }
                    };

    transport_->forEachNeighbour(std::move(callback));
    if (actualConfidants.size() % 2 == 0) {
        auto it = actualConfidants.end();
        --it;
        it = actualConfidants.erase(it);

    }
    initBootstrapRP(actualConfidants);


    cslog() << "NODE> Bootstrap available nodes [" << actualConfidants.size() << "]:";
    for (const auto& item : actualConfidants) {
        cslog() << "NODE> " << " - " << KeyToBase58(item) << (item == own_key ? " (me)" : "");
    }

    if (actualConfidants.size() < initialConfidants_.size()) {
        cslog() << "Num of confidants with max sequence " << maxGlobalBlock
                << " (" << actualConfidants.size() << " is less than init trusted num "
                << initialConfidants_.size() << ", start lookup...";

        transport_->addToNeighbours(initialConfidants_);
    }

    if (actualConfidants.size() < Consensus::MinTrustedNodes) {
        cslog() << "Not enough confidants with max sequence " << maxGlobalBlock
            << " (" << actualConfidants.size() << ", min " << Consensus::MinTrustedNodes
            << " required). Wait until syncro finished or more bootstrap nodes to start...";

        return;
    }

    if (actualConfidants.find(own_key) == actualConfidants.cend()) {
        cslog() << "Should not start rounds, local block " << maxLocalBlock << ", global block " << maxGlobalBlock;
        return;
    }

    if (roundPackageCache_.empty()) {
        cserror() << "Cannot start rounds, round package cache is empty.";
        return;
    }

    // do not increment, only "mark" default round start
    subRound_ = 1;

    if (*actualConfidants.cbegin() == own_key) {

        cslog() << "Starting round...";

        cs::Bytes bin;
        cs::ODataStream out(bin);
        out << uint8_t(actualConfidants.size());

        for (const auto& item : actualConfidants) {
            out << item; 
        }
        cs::PublicKeys confs;
        for (auto it : actualConfidants) {
            confs.push_back(it);
        }
        uint8_t currentWeight = calculateBootStrapWeight(confs);
        if (currentWeight > bootStrapWeight_) {
            bootStrapWeight_ = currentWeight;
        }
        // when we try to start rounds several times, we will not send duplicates
        auto random = std::random_device{}();
        out << random;

        auto& conveyer = cs::Conveyer::instance();
        conveyer.updateRoundTable(roundPackageCache_.back().roundTable().round, roundPackageCache_.back().roundTable());

        sendBroadcast(MsgTypes::BootstrapTable, roundPackageCache_.back().roundTable().round, bin);
        confirmationList_.remove(roundPackageCache_.back().roundTable().round);
        if (!isBootstrapRound_) {
            isBootstrapRound_ = true;
            cslog() << "NODE> Bootstrap on, sending bootstrap table";
        }

        onRoundStart(roundPackageCache_.back().roundTable(), true);
        reviewConveyerHashes();
    }
    else {
        cslog() << "Wait for " << KeyToBase58(*actualConfidants.cbegin()) << " to start round...";
    }
}

bool Node::bootstrap(const cs::Bytes& bytes, cs::RoundNumber round) {
    std::set<cs::PublicKey> confidants;
    cs::IDataStream input(bytes.data(), bytes.size());
    uint8_t boot_cnt = 0;
    input >> boot_cnt;
    const size_t req_len = 1 + size_t(boot_cnt) * cscrypto::kPublicKeySize + cscrypto::kSignatureSize;
    if (bytes.size() != req_len) {
        csdebug() << kLogPrefix_ << "malformed bootstrap packet, drop";
        return false;
    }
    for (size_t i = 0; i < size_t(boot_cnt); ++i) {
        cs::PublicKey key;
        input >> key;
        confidants.insert(key);
    }
    cs::Signature sig;
    input >> sig;

    cslog() << "NODE> Bootstrap available nodes [" << confidants.size() << "]:";
    for (const auto& item : confidants) {
        const auto beg = item.data();
        const auto end = beg + item.size();
        cslog() << "NODE> " << " - " << EncodeBase58(beg, end);
    }

    initBootstrapRP(confidants);
    cs::RoundPackage rp;
    cs::RoundTable rt;
    rt.round = std::max(getBlockChain().getLastSeq() + 1, round);
    for (auto& key : confidants) {
        rt.confidants.push_back(key);
    }
    rp.updateRoundTable(rt);
    roundPackageCache_.push_back(rp);

    cslog() << "Bootstrap round " << rt.round << "...";

    auto& conveyer = cs::Conveyer::instance();
    conveyer.updateRoundTable(roundPackageCache_.back().roundTable().round, roundPackageCache_.back().roundTable());

    uint8_t currentWeight = calculateBootStrapWeight(rt.confidants);
    if (currentWeight > bootStrapWeight_) {
        bootStrapWeight_ = currentWeight;
    }

    sendBroadcast(MsgTypes::BootstrapTable, roundPackageCache_.back().roundTable().round, bytes);
    if (!isBootstrapRound_) {
        isBootstrapRound_ = true;
        cslog() << "NODE> Bootstrap on, sending bootstrap table";
    }
    confirmationList_.remove(roundPackageCache_.back().roundTable().round);
    onRoundStart(roundPackageCache_.back().roundTable(), true);
    reviewConveyerHashes();

    return true;
}

void Node::getKnownPeers(std::vector<api_diag::ServerNode>& nodes) {
    // assume call from processorRoutine() as mentioned in header comment
    std::vector<cs::PeerData> peers;
    transport_->getKnownPeers(peers);
    for (const auto& peer : peers) {
        api_diag::ServerNode node;
        node.__set_ip(peer.ip);
        node.__set_port(std::to_string(peer.port));
        node.__set_publicKey(peer.id);
        node.__set_version(std::to_string(peer.version));
        node.__set_platform(std::to_string(peer.platform));
        node.__set_countTrust(0);
        //node.__set_hash(""); // ???
        node.__set_timeActive(0);
        node.__set_timeRegistration(0);

        cs::Bytes bytes;
        if (DecodeBase58(peer.id, bytes)) {
            cs::PublicKey key;
            if (key.size() == bytes.size()) {
                std::copy(bytes.cbegin(), bytes.cend(), key.begin());
                if (key == nodeIdKey_) {
                    node.__set_platform(std::to_string(csconnector::connector::platform()));
                    node.__set_version(std::to_string(NODE_VERSION));
                }

#if defined(MONITOR_NODE)
                blockChain_.iterateOverWriters([&](const cs::PublicKey& k, const cs::WalletsCache::TrustedData& d) {
                    if (k == key) {
                        node.__set_countTrust(static_cast<int32_t>(d.times_trusted));
                        // d.times; - (senseless) count to be writer
                        return false; // stop loop
                    }
                    return true;
                });
#endif // MONITOR_NODE
            }
        }

        nodes.push_back(node);
    }

}


void Node::updateWithPeerData(std::map<cs::PublicKey, cs::NodeStat>& sNodes) {
    std::vector<cs::PeerData> peers;
    transport_->getKnownPeers(peers);
    for (const auto& peer : peers) {
        cs::PublicKey key;
        cs::Bytes kBytes;
        cs::Bytes bytes;
        if (DecodeBase58(peer.id, bytes)) {
            cs::PublicKey key;
            if (key.size() == bytes.size()) {
                std::copy(bytes.cbegin(), bytes.cend(), key.begin());
                auto it = sNodes.find(key);
                if (it != sNodes.end()) {
                    it->second.ip = peer.ip;
                    it->second.version = std::to_string(peer.version);
                    it->second.platform = std::to_string(peer.platform);
                }
            }
        }
    }
    auto it = sNodes.find(nodeIdKey_);
    if (it != sNodes.end()) {
        it->second.ip = cs::ConfigHolder::instance().config()->getAddressEndpoint().ipSpecified 
            ? cs::ConfigHolder::instance().config()->getAddressEndpoint().ip 
            : "";
        it->second.version = std::to_string(NODE_VERSION);
        it->second.platform = std::to_string(csconnector::connector::platform());
    }
    
}

api_diag::ServerTrustNode Node::convertNodeInfo(const cs::PublicKey& pKey, const cs::NodeStat& ns) {
        api_diag::ServerTrustNode node;
        node.__set_ip(ns.ip);
        node.__set_publicKey(EncodeBase58(pKey.data(), pKey.data() + pKey.size()));
        node.__set_timeActive(ns.timeActive);
        node.__set_timeRegistration(ns.timeReg);
        node.__set_platform(ns.platform);
        node.__set_version(ns.version);
        node.__set_failedTrustDay(ns.failedTrustedDay);
        node.__set_failedTrustMonth(ns.failedTrustedMonth);
        node.__set_failedTrustPrevMonth(ns.failedTrustedMonth);
        node.__set_failedTrustTotal(ns.failedTrustedTotal);
        node.__set_active(ns.nodeOn);
        node.__set_failedTrustedADay(ns.failedTrustedADay);
        node.__set_failedTrustedAMonth(ns.failedTrustedAMonth);
        node.__set_failedTrustedAPrevMonth(ns.failedTrustedAPrevMonth);
        node.__set_failedTrustedATotal(ns.failedTrustedATotal);
        general::Amount fDay;
        fDay.__set_integral(ns.feeDay.integral());
        fDay.__set_fraction(ns.feeDay.fraction());
        node.__set_feeDay(fDay);
        general::Amount fMonth;
        fMonth.__set_integral(ns.feeMonth.integral());
        fMonth.__set_fraction(ns.feeMonth.fraction());
        node.__set_feeMonth(fMonth);
        general::Amount fPrevMonth;
        fPrevMonth.__set_integral(ns.feePrevMonth.integral());
        fPrevMonth.__set_fraction(ns.feePrevMonth.fraction());
        node.__set_feePrevMonth(fPrevMonth);
        general::Amount fTotal;
        fTotal.__set_integral(ns.feeTotal.integral());
        fTotal.__set_fraction(ns.feeTotal.fraction());
        node.__set_feeTotal(fTotal);
        node.__set_trustDay(ns.trustedDay);
        node.__set_trustMonth(ns.trustedMonth);
        node.__set_trustPrevMonth(ns.trustedPrevMonth);
        node.__set_trustTotal(ns.trustedTotal);
        node.__set_trustedADay(ns.trustedADay);
        node.__set_trustedAMonth(ns.trustedAMonth);
        node.__set_trustedAPrevMonth(ns.trustedAPrevMonth);
        node.__set_trustedATotal(ns.trustedATotal);
        general::Amount rDay;
        rDay.__set_integral(ns.rewardDay.integral());
        rDay.__set_fraction(ns.rewardDay.fraction());
        node.__set_rewardDay(rDay);
        general::Amount rMonth;
        rMonth.__set_integral(ns.rewardMonth.integral());
        rMonth.__set_fraction(ns.rewardMonth.fraction());
        node.__set_rewardMonth(rMonth);
        general::Amount rPrevMonth;
        rPrevMonth.__set_integral(ns.rewardPrevMonth.integral());
        rPrevMonth.__set_fraction(ns.rewardPrevMonth.fraction());
        node.__set_rewardPrevMonth(rPrevMonth);
        general::Amount rTotal;
        rTotal.__set_integral(ns.rewardTotal.integral());
        rTotal.__set_fraction(ns.rewardTotal.fraction());
        node.__set_rewardTotal(rTotal);
        return node;
}

void Node::getKnownPeersUpd(std::vector<api_diag::ServerTrustNode>& nodes, bool oneKey, const csdb::Address& pKey) {
    auto statNodes = stat_.getNodes();
    updateWithPeerData(statNodes);
    if (oneKey) {
        auto nodeInfo = statNodes.find(pKey.public_key());
        if (nodeInfo != statNodes.end()) {
            nodes.push_back(convertNodeInfo(nodeInfo->first, nodeInfo->second));
        }
        else {
            cserror() << "Node probably not found";
        }
    }
    else {
        for (auto it : statNodes) {
            nodes.push_back(convertNodeInfo(it.first, it.second));
        }
    }


}

void Node::getNodeInfo(const api_diag::NodeInfoRequest& request, api_diag::NodeInfo& info) {
    cs::Sequence sequence = blockChain_.getLastSeq();

    // assume call from processorRoutine() as mentioned in header comment
    info.id = EncodeBase58(nodeIdKey_.data(), nodeIdKey_.data() + nodeIdKey_.size());
    info.version = std::to_string(NODE_VERSION);
    info.platform = (api_diag::Platform) csconnector::connector::platform();
    if (request.session) {
        api_diag::SessionInfo session;
        session.__set_startRound(stat_.nodeStartRound());
        session.__set_curRound(cs::Conveyer::instance().currentRoundNumber());
        session.__set_lastBlock(sequence);
        session.__set_uptimeMs(stat_.uptimeMs());
        session.__set_aveRoundMs(stat_.aveRoundMs());
        info.__set_session(session);
    }

    Transport::BanList bl;
    transport_->getBanList(bl);

    if (request.state) {
        api_diag::StateInfo state;
        state.__set_transactionsCount(stat_.totalTransactions());
        state.__set_totalWalletsCount(blockChain_.getWalletsCount());
        state.__set_aliveWalletsCount(blockChain_.getWalletsCountWithBalance());
        state.__set_contractsCount(solver_->smart_contracts().contracts_count());
        state.__set_contractsQueueSize(solver_->smart_contracts().contracts_queue_size());
        state.__set_grayListSize(solver_->grayListSize());
        state.__set_blackListSize(bl.size());
        state.__set_blockCacheSize(blockChain_.getCachedBlocksSize());
        /*
            9: StageCacheSize consensusMessage
            10: StageCacheSize contractsMessage
            11: StageCacheSize contractsStorage
        */
        api_diag::StageCacheSize cache_size;
        
        cache_size.__set_stage1(stageOneMessage_.size());
        cache_size.__set_stage2(stageTwoMessage_.size());
        cache_size.__set_stage3(stageThreeMessage_.size());
        state.__set_consensusMessage(cache_size);

        //cache_size.__set_stage1(smartStageOneMessage_.size());
        //cache_size.__set_stage2(smartStageTwoMessage_.size());
        //cache_size.__set_stage3(smartStageThreeMessage_.size());
        state.__set_contractsMessage(cache_size);

        cache_size.__set_stage1(smartStageOneStorage_.size());
        cache_size.__set_stage2(smartStageTwoStorage_.size());
        cache_size.__set_stage3(smartStageThreeStorage_.size());
        state.__set_contractsStorage(cache_size);

        info.__set_state(state);
    }
    if (request.grayListContent) {
        std::vector<std::string> gray_list;
        solver_->getGrayListContentBase58(gray_list);
        info.__set_grayListContent(gray_list);
    }
    if (request.blackListContent) {
        std::vector<std::string> black_list;
        for (const auto bl_item : bl) {
            black_list.emplace_back(bl_item.first + ':' + std::to_string(bl_item.second));
        }
        info.__set_blackListContent(black_list);
    }

    // get bootstrap nodes
    std::map<cs::PublicKey, api_diag::BootstrapNode> bootstrap;
    for (const auto& item : initialConfidants_) {
        bool alive = (item == nodeIdKey_);
        cs::Sequence seq = alive ? sequence : 0;
        api_diag::BootstrapNode bn;
        bn.__set_id(EncodeBase58(item.data(), item.data() + item.size()));
        bn.__set_alive(alive);
        bn.__set_sequence(seq);
        bootstrap[item] = bn;
    }
    // update alive bootstrap nodes
    auto callback = [&bootstrap, this](const cs::PublicKey& neighbour, cs::Sequence lastSeq, cs::RoundNumber) {
        const auto it = initialConfidants_.find(neighbour);
        if (it == initialConfidants_.end()) {
            return;
        }
        auto& item = bootstrap[*it];
        item.__set_alive(true);
        item.__set_sequence(lastSeq);
    };
    transport_->forEachNeighbour(std::move(callback));
    // store bootstrap
    std::vector<api_diag::BootstrapNode> bootstrap_list;
    for (const auto& item : bootstrap) {
        bootstrap_list.push_back(item.second);
    }
    info.__set_bootstrap(bootstrap_list);

}

void Node::getSupply(std::vector<csdb::Amount>& supply) {
    BlockChain::WalletData wData;
    BlockChain::WalletId wId;
    std::string genesis = "11111111111111111111111111111112";
    cscrypto::Bytes bytesAddr;
    csdb::Amount genBalance{ 0 };
    if (!DecodeBase58(genesis, bytesAddr)) {
        csdebug() << __func__ << ": can't convert address " << genesis;
        return;
    }
    if (!getBlockChain().findWalletData(BlockChain::getAddressFromKey(bytesAddr), wData)) {
        csdebug() << __func__ << ": can't find address " << cs::Utils::byteStreamToHex(bytesAddr);

    }
    else {
        genBalance -= wData.balance_;
    }
    supply.push_back(genBalance);

    std::string rBin = "CSoooooooooooooooooooooooooooooooooooooooooo";
    csdb::Amount binBalance{ 0 };
    if (!DecodeBase58(rBin, bytesAddr)) {
        csdebug() << __func__ << ": can't convert address " << rBin;
        return;
    }
    if (!getBlockChain().findWalletData(BlockChain::getAddressFromKey(bytesAddr), wData)) {
        csdebug() << __func__ << ": can't find address " << cs::Utils::byteStreamToHex(bytesAddr);
    }
    else {
        binBalance += wData.balance_;
    }

    supply.push_back(binBalance);

    auto mined = stat_.getMined().rewardTotal;
    supply.push_back(mined);
    auto tot = genBalance + mined - binBalance;
    supply.push_back(tot);
    //csdebug() << "Initial: " << genBalance.to_string() << ", burned: " << binBalance.to_string() << ", mined: " << mined.to_string() << ", total: " << tot.to_string();

}

void Node::getMined(std::vector<csdb::Amount>& mined) {
    auto minedStat = stat_.getMined();
    mined.push_back(minedStat.rewardDay);
    mined.push_back(minedStat.rewardMonth);
    mined.push_back(minedStat.rewardPrevMonth);
    mined.push_back(minedStat.rewardTotal);
}

void Node::getNodeRewardEvaluation(std::vector<api_diag::NodeRewardSet>& request, std::string& msg, const cs::PublicKey& pKey, bool oneNode) {
    auto statNodes = stat_.getRoundEvaluation();
    if (oneNode) {
        auto it = statNodes.find(pKey);
        if (it == statNodes.end()) {
            msg = "Not found";
            return;
        }
        api_diag::NodeRewardSet rew;
        rew.nodeAddress = std::string(pKey.begin(), pKey.end());
        std::vector< api_diag::DelegatorRewardSet> dRew;
        for (auto a : it->second.me) {
            api_diag::DelegatorRewardSet dSet;
            dSet.__set_delegatorAddress(std::string(a.first.begin(), a.first.end()));
            dSet.rewardDay.__set_integral(a.second.rewardDay.integral());
            dSet.rewardDay.__set_fraction(a.second.rewardDay.fraction());

            dSet.rewardMonth.__set_integral(a.second.rewardMonth.integral());
            dSet.rewardMonth.__set_fraction(a.second.rewardMonth.fraction());

            dSet.rewardPrevMonth.__set_integral(a.second.rewardPrevMonth.integral());
            dSet.rewardPrevMonth.__set_fraction(a.second.rewardPrevMonth.fraction());

            dSet.rewardTotal.__set_integral(a.second.rewardTotal.integral());
            dSet.rewardTotal.__set_fraction(a.second.rewardTotal.fraction());
            dRew.push_back(dSet);
        }

        rew.__set_delegators(dRew);
        request.push_back(rew);
    }
}



uint8_t Node::requestKBAnswer(std::vector<std::string> choice) {
    if (choice.size() < 2) {
        return static_cast<uint8_t>(255);
    }
    csinfo() << "NODE> Choose the action for node to do:";
    int cnt = 1;
    for (auto it : choice) {
        csinfo() << cnt++ << ". " << it;
    }
        
    char letterKey;
    size_t ia = 0;
    while (true) {
        std::cin >> letterKey;
        ia = letterKey - '0';
        if( ia > choice.size() || ia == 0){
            csinfo() << "NODE> You've chosen not correct letter. Try again.";
        }
        else {
            break;
        }
    }
    return static_cast<uint8_t>(ia);
}

void Node::tryResolveHashProblems() {
    csinfo() << "NODE> This node db is corrupted. The problem blocks are:";
    auto it = getBlockChain().getIncorrectBlockNumbers()->begin();
    while(it != getBlockChain().getIncorrectBlockNumbers()->end()) {
        csinfo() << *it;
        ++it;
    }

    uint8_t lKey = requestKBAnswer({"resolve incorrect blocks", "go as is", "quit"});
    if (lKey == 1) {
        csinfo() << "NODE> You've chosen to resolve incorrect blocks. Wait while the node will try to perform all possible variants";
        //now we will try to eliminate the incorrect blocks from your db
        cacheLBs_ = true;//getBlockChain().cacheLastBlocks();
    }
    else if (lKey == 2) {
        csinfo() << "NODE> You've chosen go on as is. So, have a good work!";
        //just continue the normal node work
    }

    else if (lKey == 3) {
        csinfo() << "NODE> The node will quit now";
        stopRequested_ = true;
        stop();

    }

}

void Node::sendNecessaryBlockRequest(csdb::PoolHash hash, cs::Sequence seq) {
    csdebug() << __func__ << ": " << seq;
    //neededHash_ = hash;
    //neededSequence_ = seq;
    //goodAnswers_ = 0;
    //const auto round = cs::Conveyer::instance().currentRoundNumber();
    //cs::PoolsRequestedSequences sequences;
    //sequences.push_back(seq);
    //requestedKeys_.clear();
    //requestedKeys_ = poolSynchronizer_->getNeededNeighbours(seq);
    //if (!requestedKeys_.empty()) {
    //    csdebug() << "Request sent to:";
    //    for (auto k : requestedKeys_) {
    //        BaseFlags flags = static_cast<BaseFlags>(BaseFlags::Signed | BaseFlags::Compressed);
    //        transport_->sendDirect(formPacket(flags, MsgTypes::BlockRequest, round, sequences), k);
    //        csdebug() << cs::Utils::byteStreamToHex(k);
    //    }
    //}
    //else {

    //}

}

void Node::getNecessaryBlockRequest(cs::PoolsBlock& pBlock, const cs::PublicKey& sender) {
    csdebug() << __func__ << ": " << pBlock.back().sequence();
    auto it = std::find(requestedKeys_.begin(), requestedKeys_.end(), sender);
    requestedKeys_.erase(it);
    if (neededHash_== csdb::PoolHash() || pBlock.back().hash() == neededHash_) {
        if (goodAnswers_ == 0) {
            getBlockChain().replaceCachedIncorrectBlock(pBlock.back());
        }
        ++goodAnswers_;
    }
    else {
        csinfo() << "Neighbour " << cs::Utils::byteStreamToHex(sender) << " hasn'r sent the proper block";

    }
    if (requestedKeys_.size() == 0 && goodAnswers_ == 0) {
        csinfo() << "We have to find proper neighbours - current do not have valid chains";
        //TODO - change neighbours
    }

}

void Node::accountInitiationRequest(uint64_t& aTime, cs::PublicKey key) {
    std::string str = std::string(key.data(), key.data() + key.size());
    auto addr = blockChain_.getAddressFromKey(str);
    blockChain_.getAccountRegTime(aTime, addr);
}
