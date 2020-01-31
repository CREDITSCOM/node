#include <algorithm>
#include <csignal>
#include <numeric>
#include <random>
#include <sstream>
#include <numeric>

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
, stat_()
, blockValidator_(std::make_unique<cs::BlockValidator>(*this))
, observer_(observer) {
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
    cs::Connector::connect(&blockChain_.storeBlockEvent, this, &Node::processSpecialInfo);

    initPoolSynchronizer();
    setupNextMessageBehaviour();
    setupPoolSynchronizerBehaviour();

    good_ = init();
}

Node::~Node() {
    std::cout << "Desturctor called\n";

    sendingTimer_.stop();

    delete solver_;
    delete transport_;
    delete poolSynchronizer_;
}

bool Node::init() {
    auto& initConfidants = cs::ConfigHolder::instance().config()->getInitialConfidants();
    initialConfidants_ = decltype(initialConfidants_)(initConfidants.begin(), initConfidants.end());

    if (initialConfidants_.find(solver_->getPublicKey()) != initialConfidants_.end()) {
        transport_->setPermanentNeighbours(initialConfidants_);
    }

#ifdef NODE_API
    std::cout << "Init API... ";

    api_ = std::make_unique<csconnector::connector>(blockChain_, solver_);

    std::cout << "Done\n";

    cs::Connector::connect(&blockChain_.readBlockEvent(), api_.get(), &csconnector::connector::onReadFromDB);
    cs::Connector::connect(&blockChain_.storeBlockEvent, api_.get(), &csconnector::connector::onStoreBlock);
    cs::Connector::connect(&blockChain_.startReadingBlocksEvent(), api_.get(), &csconnector::connector::onMaxBlocksCount);
    cs::Connector::connect(&cs::Conveyer::instance().packetExpired, api_.get(), &csconnector::connector::onPacketExpired);
    cs::Connector::connect(&cs::Conveyer::instance().transactionsRejected, api_.get(), &csconnector::connector::onTransactionsRejected);

#endif  // NODE_API

    // must call prior to blockChain_.init():
    solver_->init(nodeIdKey_, nodeIdPrivate_);
    solver_->startDefault();

    if (cs::ConfigHolder::instance().config()->newBlockchainTop()) {
        if (!blockChain_.init(cs::ConfigHolder::instance().config()->getPathToDB(), cs::ConfigHolder::instance().config()->newBlockchainTopSeq())) {
            return false;
        }
        return true;
    }

    if (!blockChain_.init(cs::ConfigHolder::instance().config()->getPathToDB())) {
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

    initBootstrapRP(initialConfidants_);
    EventReport::sendRunningStatus(*this, Running::Status::Run);
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

void Node::setupPoolSynchronizerBehaviour() {
    cs::Connector::connect(&blockChain_.storeBlockEvent, &stat_, &cs::RoundStat::onBlockStored);
    cs::Connector::connect(&stat_.storeBlockTimeElapsed, poolSynchronizer_, &cs::PoolSynchronizer::onStoreBlockTimeElapsed);
}

void Node::run() {
    std::cout << "Running transport\n";
    transport_->run();
}

void Node::stop() {
    // stopping transport stops the node (see Node::run() method)
    transport_->stop();
    cswarning() << "[TRANSPORT STOPPED]";
}

void Node::destroy() {
    EventReport::sendRunningStatus(*this, Running::Status::Stop);

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

void Node::getBootstrapTable(const uint8_t* data, const size_t size, const cs::RoundNumber rNum) {
    cslog() << "NODE> get Boot strap Round Table #" << rNum;

    cs::IDataStream in(data, size);
    cs::RoundTable roundTable;
    cs::Bytes bin;
    in >> bin;
    cs::IDataStream stream(bin.data(), bin.size());

    uint8_t confSize = 0;
    stream >> confSize;
    csdebug() << "NODE> Number of confidants :" << cs::numeric_cast<int>(confSize);

    if (confSize < Consensus::MinTrustedNodes || confSize > Consensus::MaxTrustedNodes) {
        cswarning() << "Bad confidants num";
        return;
    }

    cs::ConfidantsKeys confidants;
    confidants.reserve(confSize);

    for (int i = 0; i < confSize; ++i) {
        cs::PublicKey key;
        stream >> key;
        confidants.push_back(std::move(key));
    }

    if (!stream.isValid() || confidants.size() < confSize) {
        cswarning() << "Bad round table format, ignoring";
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

    cs::Conveyer::instance().updateRoundTable(rNum, roundTable);
    onRoundStart(roundTable, false);
    reviewConveyerHashes();

    poolSynchronizer_->sync(rNum);
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
            //    transport_->unmarkNeighbourAsBlackListed(it->first);
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

    csdebug() << "NODE> Get request for " << hashes.size() << " packet hashes from " << cs::Utils::byteStreamToHex(sender.data(), sender.size());

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
        << ", timestamp " << rPackage.poolMetaInfo().timestamp;

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
        if (isBootstrapRound()) {
            BlockChain::setBootstrap(tmpPool, true);
        }
        tmpPool.add_number_trusted(static_cast<uint8_t>(rPackage.poolMetaInfo().realTrustedMask.size()));
        tmpPool.add_real_trusted(cs::Utils::maskToBits(rPackage.poolMetaInfo().realTrustedMask));
        tmpPool.set_signatures(tmp2);
        csdebug() << "Signatures " << tmp2.size() << " were added to the pool: " << tmpPool.signatures().size();
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
    if (!blockChain_.storeBlock(pool.value(), false /*by_sync*/)) {
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
    if (!conf.on) {
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
        if (!DecodeBase58(conf.collector_ep.id, publicKey)) {
            cserror() << "Wrong collector id.";
            return;
        }
        std::copy(publicKey.begin(), publicKey.end(), receiver.begin());
    }

    transport_->sendDirect(formPacket(BaseFlags::Signed, MsgTypes::EventReport, cs::Conveyer::instance().currentRoundNumber(), sig, message),
                           receiver);
    csmeta(csdebug) << "event report -> " << conf.collector_ep.ip << ':' << conf.collector_ep.port;
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
                    << " updated status to " << Running::to_string(status);
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

void Node::sendTransactionsPacket(const cs::TransactionsPacket& packet) {
    if (packet.hash().isEmpty()) {
        cswarning() << "Send transaction packet with empty hash failed";
        return;
    }
    csdebug() << "NODE> Sending transaction's packet with hash: " << cs::Utils::byteStreamToHex(packet.hash().toBinary().data(), packet.hash().size());
    sendBroadcast(MsgTypes::TransactionPacket, cs::Conveyer::instance().currentRoundNumber(), packet);
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

void Node::getBlockRequest(const uint8_t* data, const size_t size, const cs::PublicKey& sender) {
    csmeta(csdebug);

    cs::PoolsRequestedSequences sequences;

    cs::IDataStream stream(data, size);
    stream >> sequences;

    csdebug() << "NODE> got request for " << sequences.size() << " block(s) from " << cs::Utils::byteStreamToHex(sender.data(), sender.size());

    if (sequences.empty()) {
        csmeta(cserror) << "Sequences size is 0";
        return;
    }

    if (sequences.front() > blockChain_.getLastSeq()) {
        csdebug() << "NODE> Get block request> The requested block: " << sequences.front() << " is beyond my last block";
        return;
    }

    const std::size_t reserveSize = sequences.size();

    cs::PoolsBlock poolsBlock;
    poolsBlock.reserve(reserveSize);

    auto sendReply = [&] {
        sendBlockReply(poolsBlock, sender);
        poolsBlock.clear();
    };

    for (auto& sequence : sequences) {
        csdb::Pool pool = blockChain_.loadBlock(sequence);

        if (pool.is_valid()) {
            poolsBlock.push_back(std::move(pool));
        }
        else {
            csmeta(cslog) << "unable to load block " << sequence << " from blockchain";
        }
    }

    sendReply();
}

void Node::getBlockReply(const uint8_t* data, const size_t size) {
    if (!poolSynchronizer_->isSyncroStarted()) {
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

    poolSynchronizer_->getBlockReply(std::move(poolsBlock));
}

void Node::sendBlockReply(const cs::PoolsBlock& poolsBlock, const cs::PublicKey& target) {
    if (poolsBlock.empty()) {
        return;
    }

    for (const auto& pool : poolsBlock) {
        csdetails() << "NODE> Send block reply. Sequence: " << pool.sequence();
    }
    
    csdebug() << "Node> Sending " << poolsBlock.size() << " blocks with signatures from " << poolsBlock.front().sequence() << " to " << poolsBlock.back().sequence();

    for (const auto& it : poolsBlock) {
        csdetails() << "#" << it.sequence() << " signs = " << it.signatures().size();
    }

    auto region = compressor_.compress(poolsBlock);
    sendDirect(target, MsgTypes::RequestedBlock, cs::Conveyer::instance().currentRoundNumber(), region);
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
    const auto lastSequence = blockChain_.getLastSeq();
    const auto round = cs::Conveyer::instance().currentRoundNumber();

    if (stat_.isCurrentRoundTooLong(kLastPoolSynchroDelay_) && round < lastSequence + cs::PoolSynchronizer::roundDifferentForSync) {
        poolSynchronizer_->syncLastPool();
    }
    else {
        poolSynchronizer_->sync(round, cs::PoolSynchronizer::roundDifferentForSync);
    }
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
    CallsQueue::instance().insert(std::bind(&Node::sendTransactionsPacket, this, packet));
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

void Node::sendBlockRequest(const cs::PublicKey& target, const cs::PoolsRequestedSequences& sequences) {
    const auto round = cs::Conveyer::instance().currentRoundNumber();
    csmeta(csdetails) << "Target out(): " << ", sequence from: " << sequences.front()
                      << ", to: " << sequences.back() << ", round: " << round;

    BaseFlags flags = static_cast<BaseFlags>(BaseFlags::Signed | BaseFlags::Compressed);
    transport_->sendDirect(formPacket(flags, MsgTypes::BlockRequest, round, sequences), target);
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
        case MsgTypes::TransactionsPacketRequest:
        case MsgTypes::TransactionsPacketReply:
        case MsgTypes::RoundTableRequest:
        case MsgTypes::RejectedContracts:
        case MsgTypes::RoundPackRequest:
        case MsgTypes::EmptyRoundPack:
        case MsgTypes::StateRequest:
        case MsgTypes::StateReply:
        case MsgTypes::BlockAlarm:
        case MsgTypes::EventReport:
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

        if (rNum > blockChain_.getLastSeq() + cs::Conveyer::HashTablesStorageCapacity) {
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
void Node::sendConfidants(const MsgTypes msgType, const cs::RoundNumber round, Args&&... args) {
    csdebug() << "NODE> Sending confidants, round: " << round
              << ", msgType: " << Packet::messageTypeToString(msgType);

    sendDirect(cs::Conveyer::instance().confidants(), msgType, round, std::forward<Args>(args)...);
}

void Node::sendStageOne(const cs::StageOne& stageOneInfo) {
    if (myLevel_ != Level::Confidant) {
        cswarning() << "NODE> Only confidant nodes can send consensus stages";
        return;
    }

    csmeta(csdebug) << "Round: " << cs::Conveyer::instance().currentRoundNumber() << "." << cs::numeric_cast<int>(subRound_)
        << cs::StageOne::toString(stageOneInfo);

    csdebug() << "Stage one Message R-" << cs::Conveyer::instance().currentRoundNumber() << "[" << static_cast<int>(stageOneInfo.sender)
        << "]: " << cs::Utils::byteStreamToHex(stageOneInfo.message.data(), stageOneInfo.message.size());
    csdebug() << "Stage one Signature R-" << cs::Conveyer::instance().currentRoundNumber() << "[" << static_cast<int>(stageOneInfo.sender)
        << "]: " << cs::Utils::byteStreamToHex(stageOneInfo.signature.data(), stageOneInfo.signature.size());

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
        csdebug() << kLogPrefix_ << "ignore stage-1 as no confidant";
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
    csdebug() << kLogPrefix_ << "StageOne Message[" << static_cast<int>(stage.sender) <<"]: " << cs::Utils::byteStreamToHex(stage.message.data(), stage.message.size());
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
        csdebug() << kLogPrefix_ << "Ignore StageTwo as no confidant";
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
        csdebug() << "NODE> ignore stage-3 as no confidant";
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

    if (cs::ConfigHolder::instance().config()->isCompatibleVersion()) {
        sendDirect(conveyer.confidantByIndex(respondent), msgType, cs::Conveyer::instance().currentRoundNumber(), subRound_, myConfidantIndex_, required);
    }
    else {
        sendDirect(conveyer.confidantByIndex(respondent), msgType, cs::Conveyer::instance().currentRoundNumber(), subRound_, myConfidantIndex_, required, iteration);
    }

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

    sendDirect(conveyer.confidantByIndex(requester), msgType, cs::Conveyer::instance().currentRoundNumber(), subRound_, signature, message);
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

    sendDirect(smartConfidants, MsgTypes::FirstSmartStage, cs::Conveyer::instance().currentRoundNumber(),
               stageOneInfo.message, stageOneInfo.signature);

    csmeta(csdebug) << "done";
}

void Node::getSmartStageOne(const uint8_t* data, const size_t size, const cs::RoundNumber, const cs::PublicKey& sender) {
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

    if (std::find(activeSmartConsensuses_.cbegin(), activeSmartConsensuses_.cend(), stage.id) == activeSmartConsensuses_.cend()) {
        csdebug() << "The SmartConsensus {" << block << '.' << transaction << "} is not active now, storing the stage";
        smartStageOneStorage_.push_back(stage);
        return;
    }

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
    sendDirect(smartConfidants, MsgTypes::SecondSmartStage, cs::Conveyer::instance().currentRoundNumber(), bytes, stageTwoInfo.signature);

    // cash our stage two
    stageTwoInfo.message = std::move(bytes);
    csmeta(csdebug) << "done";
}

void Node::getSmartStageTwo(const uint8_t* data, const size_t size, const cs::RoundNumber, const cs::PublicKey& sender) {
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

    sendDirect(smartConfidants, MsgTypes::ThirdSmartStage, cs::Conveyer::instance().currentRoundNumber(),
               // payload:
              bytes, stageThreeInfo.signature);

    // cach stage three
    stageThreeInfo.message = std::move(bytes);
    csmeta(csdebug) << "done";
}

void Node::getSmartStageThree(const uint8_t* data, const size_t size, const cs::RoundNumber, const cs::PublicKey& sender) {
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
    sendDirect(confidant, msgType, cs::Conveyer::instance().currentRoundNumber(), smartID, respondent, required);
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

    sendDirect(requester, msgType, cs::Conveyer::instance().currentRoundNumber(), message, signature);
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

    sendDirect(target, MsgTypes::RoundTable, rpCurrent->roundTable().round, rpCurrent->subRound(), rpCurrent->toBinary());
    csdebug() << "Done";

    if (!rpCurrent->poolMetaInfo().characteristic.mask.empty()) {
        csmeta(csdebug) << "Packing " << rpCurrent->poolMetaInfo().characteristic.mask.size() << " bytes of char. mask to send";
    }

    return true;
}

void Node::sendRoundPackageToAll(cs::RoundPackage& rPackage) {
    // add signatures// blockSignatures, roundSignatures);
    csmeta(csdetails) << "Send round table to all";
    
    sendBroadcast(MsgTypes::RoundTable, rPackage.roundTable().round, rPackage.subRound(), rPackage.toBinary());

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

    cs::Bytes roundBytes = rPackage.bytesToSign();
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
        uint64_t currentTimeStamp;
        [[maybe_unused]] uint64_t rpTimeStamp;
        try {
            std::string lTS = getBlockChain().getLastTimeStamp();
            lastTimeStamp = std::stoull(lTS.empty() == 0 ? "0" : lTS);
        }
        catch (...) {
            csdebug() << __func__ << ": last block Timestamp was announced as zero";
            return false;
        }

        try {
            currentTimeStamp = std::stoull(cs::Utils::currentTimestamp());
        }
        catch (...) {
            csdebug() << __func__ << ": current Timestamp was announced as zero";
            return false;
        }

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

            const auto ave_duration = stat_.aveTime();
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
            return true;
        }
    }
    return false;
}

void Node::getRoundTable(const uint8_t* data, const size_t size, const cs::RoundNumber rNum, const cs::PublicKey& sender) {
    csdebug() << "NODE> get round table R-" << WithDelimiters(rNum) << " from " << cs::Utils::byteStreamToHex(sender.data(), sender.size());
    csmeta(csdetails) << "started";

    if (myLevel_ == Level::Writer) {
        csmeta(cserror) << "Writers don't receive round table";
        return;
    }

    cs::IDataStream stream(data, size);

    // RoundTable evocation
    cs::Byte subRound = 0;
    stream >> subRound;

    // sync state check
    cs::Conveyer& conveyer = cs::Conveyer::instance();

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

    processSync();

    if (poolSynchronizer_->isSyncroStarted()) {
        getCharacteristic(rPackage);
    }

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
                    --it;
                    roundPackageCache_.erase(it);
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
    getCharacteristic(rPackage);

    try {
        lastRoundPackageTime_ = std::stoull(cs::Utils::currentTimestamp());
    }
    catch (...) {
        csdebug() << __func__ << ": current Timestamp was announced as zero";
        return;
    }

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
    size_t justTime;

    try {
        justTime = std::stoull(cs::Utils::currentTimestamp());
    }
    catch (...) {
        csdebug() << __func__ << ": current Timestamp was announced as zero";
        return false;
    }

    if (justTime > lastRoundPackageTime_) {
        if (justTime - lastRoundPackageTime_ > Consensus::MaxRoundDuration) {
            csdebug() << "NODE> The current round lasts too long, possible traffic problems";
            return false; //
        }
        else {
            bool condition = (!poolSynchronizer_->isSyncroStarted()) && (cs::Conveyer::instance().currentRoundNumber() 
                - getBlockChain().getLastSeq() < cs::PoolSynchronizer::roundDifferentForSync);
            return condition;
        }
    }
    else {
        csdebug() << "NODE> Possible wrong node clock";
        return false;
    }
}

void Node::clearRPCache(cs::RoundNumber rNum) {
    bool flagg = true;
    if (rNum < 6) {
        return;
    }
    while (flagg) {
        auto tmp = std::find_if(roundPackageCache_.begin(), roundPackageCache_.end(), [rNum](cs::RoundPackage& rp) {return rp.roundTable().round == rNum - 5; });
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
        currentTimeStamp = std::stoull(cs::Utils::currentTimestamp());
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

    if (confidants.size() < Consensus::MinTrustedNodes) {
        sendBroadcast(MsgTypes::BlockHash, round, subRound_, messageToSend, signature);
        return;
    }

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
        currentTimeStamp = std::stoull(cs::Utils::currentTimestamp());
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
    sendDirect(respondent, MsgTypes::RoundTable, rp.roundTable().round, rp.subRound(), rp.toBinary());
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
    }
    else {
        line1 << "TRUSTED [" << cs::numeric_cast<int>(myConfidantIndex_) << "]";
    }

    line1 << ' ';

    for (int i = 0; i < padWidth; i++) {
        line1 << '=';
    }

    const auto s = line1.str();
    const std::size_t fixedWidth = s.size();

    cslog() << s;
    csdebug() << " Node key " << cs::Utils::byteStreamToHex(nodeIdKey_);
    cslog() << " Last written sequence = " << WithDelimiters(blockChain_.getLastSeq()) << ", neighbour nodes = " << transport_->getNeighboursCount();

    std::ostringstream line2;

    for (std::size_t i = 0; i < fixedWidth; ++i) {
        line2 << '-';
    }

    csdebug() << line2.str();
    csdebug() << " Confidants:";
    for (size_t i = 0; i < roundTable.confidants.size(); ++i) {
        auto result = myLevel_ == Level::Confidant && i == myConfidantIndex_;
        auto name = result ? "me" : cs::Utils::byteStreamToHex(roundTable.confidants[i]);

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
    /*bool notused =*/ sendDirect(respondent, MsgTypes::HashReply, cs::Conveyer::instance().currentRoundNumber(), subRound_, signature, getConfidantNumber(), hash);
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

    if (static_cast<size_t>(badHashReplySummary) > conveyer.confidantsCount() / 2 && !lastBlockRemoved_) {
        csmeta(csdebug) << "This node really have not valid HASH!!! Removing last block from DB and trying to syncronize";
        // TODO: examine what will be done without this function
        if (!roundPackageCache_.empty() && roundPackageCache_.back().poolMetaInfo().realTrustedMask.size() > cs::TrustedMask::trustedSize(roundPackageCache_.back().poolMetaInfo().realTrustedMask)) {
            blockChain_.setBlocksToBeRemoved(1U);
            blockChain_.removeLastBlock();
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


void Node::processSpecialInfo(const csdb::Pool& pool) {
    for (auto it : pool.transactions()) {
        if (getBlockChain().isSpecial(it)) {
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
        }
    }

}

void Node::validateBlock(const csdb::Pool& block, bool* shouldStop) {
    if (stopRequested_) {
        *shouldStop = true;
        return;
    }
    if (!blockValidator_->validateBlock(block,
        cs::BlockValidator::ValidationLevel::hashIntergrity
            /*| cs::BlockValidator::ValidationLevel::smartStates*/
            /*| cs::BlockValidator::ValidationLevel::accountBalance*/,
        cs::BlockValidator::SeverityLevel::onlyFatalErrors)) {
        *shouldStop = true;
        return;
    }
    processSpecialInfo(block);
}

void Node::deepBlockValidation(csdb::Pool block, bool* check_failed) {//check_failed should be FALSE of the block is ok 
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
    
    constexpr const uint64_t uuidTestNet = 5283967947175248524ull;
    constexpr const uint64_t uuidMainNet = 11024959585341937636ull;
    /*constexpr*/ const bool collectRejectedInfo = cs::ConfigHolder::instance().config()->isCompatibleVersion();
    const char* kLogPrefix = (collectRejectedInfo ? "NODE> skip block validation: " : "NODE> stop block validation: ");

    if (block.sequence() <= 27020000) {
        if (getBlockChain().uuid() == uuidMainNet) {
            // valid blocks
            return;
        }
        if (block.sequence() <= 5504545) {
            if (getBlockChain().uuid() == uuidTestNet) {
                // valid blocks
                return;
            }
        }
    }

    if (smartPacks.size() != smartSignatures.size()) {
        // there was known accident in testnet only in block #2'651'597 that contains unsigned smart contract states packet
        //if (getBlockChain().uuid() == uuidTestNet) {
        cserror() << kLogPrefix << "different size of smatrpackets and signatures in block " << WithDelimiters(block.sequence());
        *check_failed = !collectRejectedInfo;
        return;
    }

    csdebug() << "NODE> SmartPacks = " << smartPacks.size();
    auto iSignatures = smartSignatures.begin();
    for (auto& it : smartPacks) {
        csdebug() << "NODE> SmartSignatures(" << iSignatures->signatures.size() << ") for contract "<< iSignatures->smartConsensusPool << ":";
        for (auto p : iSignatures->signatures) {
            it.addSignature(p.first, p.second);
            csdebug() << "NODE> " << static_cast<int>(p.first) << ". " << cs::Utils::byteStreamToHex(p.second.data(), 64);
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
    initBootstrapRP(actualConfidants);

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

    cslog() << "NODE> Bootstrap available nodes [" << actualConfidants.size() << "]:";
    for (const auto& item : actualConfidants) {
        const auto beg = item.data();
        const auto end = beg + item.size();
        cslog() << "NODE> " << " - " << EncodeBase58(beg, end) << (item == own_key ? " (me)" : "");
    }

    if (*actualConfidants.cbegin() == own_key) {

        cslog() << "Starting round...";

        cs::Bytes bin;
        cs::ODataStream out(bin);
        out << uint8_t(actualConfidants.size());

        for (const auto& item : actualConfidants) {
            out << item; 
        }

        // when we try to start rounds several times, we will not send duplicates
        auto random = std::random_device{}();
        out << random;

        auto& conveyer = cs::Conveyer::instance();
        conveyer.updateRoundTable(roundPackageCache_.back().roundTable().round, roundPackageCache_.back().roundTable());

        sendBroadcast(MsgTypes::BootstrapTable, roundPackageCache_.back().roundTable().round, bin);
        if (!isBootstrapRound_) {
            isBootstrapRound_ = true;
            cslog() << "NODE> Bootstrap on, sending bootstrap table";
        }

        onRoundStart(roundPackageCache_.back().roundTable(), true);
        reviewConveyerHashes();
    }
    else {
        const auto beg = actualConfidants.cbegin()->data();
        const auto end = beg + actualConfidants.cbegin()->size();
        cslog() << "Wait for " << EncodeBase58(beg, end) << " to start round...";
    }
}
