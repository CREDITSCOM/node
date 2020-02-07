#include <algorithm>
#include <csignal>
#include <numeric>
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

const csdb::Address Node::genesisAddress_ = csdb::Address::from_string("0000000000000000000000000000000000000000000000000000000000000001");
const csdb::Address Node::startAddress_ = csdb::Address::from_string("0000000000000000000000000000000000000000000000000000000000000002");

Node::Node(cs::config::Observer& observer)
: nodeIdKey_(cs::ConfigHolder::instance().config()->getMyPublicKey())
, nodeIdPrivate_(cs::ConfigHolder::instance().config()->getMyPrivateKey())
, blockChain_(genesisAddress_, startAddress_, cs::ConfigHolder::instance().config()->recreateIndex())
, ostream_(&packStreamAllocator_, nodeIdKey_)
, stat_()
, blockValidator_(std::make_unique<cs::BlockValidator>(*this))
, observer_(observer) {
    autoShutdownEnabled_ = cs::ConfigHolder::instance().config()->autoShutdownEnabled();

    solver_ = new cs::SolverCore(this, genesisAddress_, startAddress_);

    std::cout << "Start transport... ";
    transport_ = new Transport(this);
    std::cout << "Done\n";

    poolSynchronizer_ = new cs::PoolSynchronizer(transport_, &blockChain_);

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
    cs::Connector::connect(&transport_->pingReceived, this, &Node::onPingReceived);
    cs::Connector::connect(&transport_->pingReceived, &stat_, &cs::RoundStat::onPingReceived);
    cs::Connector::connect(&blockChain_.alarmBadBlock, this, &Node::sendBlockAlarmSignal);
    cs::Connector::connect(&blockChain_.tryToStoreBlockEvent, this, &Node::deepBlockValidation);
    cs::Connector::connect(&blockChain_.storeBlockEvent, this, &Node::processSpecialInfo);
    cs::Connector::connect(&blockChain_.uncertainBlock, this, &Node::sendBlockRequestToConfidants);

    setupNextMessageBehaviour();

    alwaysExecuteContracts_ = cs::ConfigHolder::instance().config()->alwaysExecuteContracts();
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
#ifdef NODE_API
    std::cout << "Init API... ";

    api_ = std::make_unique<csconnector::connector>(*this);

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

    cs::Connector::connect(&sendingTimer_.timeOut, this, &Node::processTimer);
    cs::Connector::connect(&cs::Conveyer::instance().packetFlushed, this, &Node::onTransactionsPacketFlushed);
    cs::Connector::connect(&poolSynchronizer_->sendRequest, this, &Node::sendBlockRequest);

    initCurrentRP();

    EventReport::sendRunningStatus(*this, Running::Status::Run);

    return true;
}

void Node::setupNextMessageBehaviour() {
    cs::Connector::connect(&transport_->mainThreadIterated, &stat_, &cs::RoundStat::onMainThreadIterated);
    cs::Connector::connect(&cs::Conveyer::instance().roundChanged, &stat_, &cs::RoundStat::onRoundChanged);
    cs::Connector::connect(&stat_.roundTimeElapsed, this, &Node::onRoundTimeElapsed);
}

void Node::run() {
    std::cout << "Running transport\n";
    transport_->run();
}

void Node::stop() {
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

    cs::Executor::instance().stop();
    cswarning() << "[EXECUTOR IS SIGNALED TO STOP]";

    observer_.stop();
    cswarning() << "[CONFIG OBSERVER STOPPED]";

}

void Node::initCurrentRP() {
    cs::RoundPackage rp;
    if (getBlockChain().getLastSeq() == 0) {
        cs::RoundTable rt;
        rt.round = 0;
        rp.updateRoundTable(rt);
    }
    else {
        cs::RoundTable rt;
        rt.round = getBlockChain().getLastSeq();
        rt.confidants = getBlockChain().getLastBlock().confidants();
        rp.updateRoundTable(rt);
    }
    roundPackageCache_.push_back(rp);
}

void Node::getUtilityMessage(const uint8_t* data, const size_t size) {
    //auto& conveyer = cs::Conveyer::instance();

    cswarning() << "NODE> Utility message get";

    istream_.init(data, size);
    cs::Signature sig;
    cs::Bytes msg;
    cs::RoundNumber rNum;
    istream_.skip<uint8_t>();
    istream_ >> msg >> sig ;
    
    if (!istream_.good() || !istream_.end()) {
        cserror() << "NODE> Bad Utility packet format";
        return;
    }

    //csdebug() << "Message to Verify: " << cs::Utils::byteStreamToHex(trustedToHash);
    const auto& starter_key = cs::PacketValidator::instance().getStarterKey();
    //csdebug() << "SSKey: " << cs::Utils::byteStreamToHex(starter_key.data(), starter_key.size());
    if (!cscrypto::verifySignature(sig, starter_key, msg.data(), msg.size())) {
        cswarning() << "The Utility message is incorrect: signature isn't valid";
        return;
    }

    cs::Byte order;
    cs::PublicKey pKey;
    cs::DataStream stream(msg.data(), msg.size());
    stream >> rNum;
    stream >> order;
    stream >> pKey;

    switch (order) {
        case Orders::Release:
            if (pKey == cs::Zero::key) {
                while (transport_->blackList().size() > 0) {
                    addToBlackList(transport_->blackList().front(), false);
                }
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

void Node::getBigBang(const uint8_t* data, const size_t size, const cs::RoundNumber rNum) {
    auto& conveyer = cs::Conveyer::instance();

    cswarning() << "-----------------------------------------------------------";
    cswarning() << "NODE> BigBang #" << rNum << ": last written #" << blockChain_.getLastSeq() << ", current #" << conveyer.currentRoundNumber();
    cswarning() << "-----------------------------------------------------------";

    istream_.init(data, size);

    uint8_t tmp = 0;
    istream_ >> tmp;

    if (tmp <= receivedBangs[rNum] && !(tmp < Consensus::MaxSubroundDelta && receivedBangs[rNum] > std::numeric_limits<uint8_t>::max() - Consensus::MaxSubroundDelta)) {
        cswarning() << "Old Big Bang received: " << rNum << "." << static_cast<int>(tmp) << " is <= " << rNum << "." << static_cast<int>(receivedBangs[rNum]);
        return;
    }

    // cache
    auto cachedRound = conveyer.currentRoundNumber();

    cs::Hash lastBlockHash;
    istream_ >> lastBlockHash;
       
    cs::RoundTable globalTable;
    globalTable.round = rNum;

    // not uses both subRound_ and recdBangs[], so can be called here:
    if (!readRoundData(globalTable, true)) {
        cserror() << className() << " read round data from SS failed";
        return;
    }

    if (istream_.isBytesAvailable(sizeof(long long))) {
        long long timeSS = 0;
        istream_ >> timeSS;
        auto seconds = timePassedSinceBB(timeSS);
        constexpr long long MaxBigBangAge_sec = 180;
        if (seconds > MaxBigBangAge_sec) {
            cslog() << "Elder Big Bang received of " << WithDelimiters(seconds) << " seconds age, ignore";
            return;
        }
        else {
            cslog() << "Big Bang received of " << WithDelimiters(seconds) << " seconds age, accept";
        }
    }
    else {
        cswarning() << "Deprecated Big Bang received of unknown age, ignore";
        return;
    }

    // update round data
    subRound_ = tmp;
    receivedBangs[rNum] = subRound_;

    if (stat_.isCurrentRoundTooLong()) {
        poolSynchronizer_->syncLastPool();
    }

    solver_->resetGrayList();
    roundPackageCache_.clear();

    // this evil code sould be removed after examination
    cs::Sequence countRemoved = 0;
    cs::Sequence lastSequence = blockChain_.getLastSeq();

    while (lastSequence >= rNum) {
        if (countRemoved == 0) {
            // the 1st time
            csdebug() << "NODE> remove " << lastSequence - rNum + 1 << " block(s) required (rNum = " << rNum << ", last_seq = " << lastSequence << ")";
            blockChain_.setBlocksToBeRemoved(lastSequence - rNum  + 1);
        }

        blockChain_.removeLastBlock();
        cs::RoundNumber tmp_seq = blockChain_.getLastSeq();

        if (lastSequence == tmp_seq) {
            csdebug() << "NODE> cancel remove blocks operation (last removal is failed)";
            break;
        }

        ++countRemoved;
        lastSequence = tmp_seq;
    }

    if (countRemoved > 0) {
        csdebug() << "NODE> " << countRemoved << " block(s) was removed";
    }

    // resend all this round data available
    csdebug() << "NODE> resend last block hash after BigBang";

    // do not pass further the hashes from unsuccessful round
    csmeta(csdebug) << "Get BigBang globalTable.hashes: " << globalTable.hashes.size();

    conveyer.updateRoundTable(cachedRound, globalTable);
    onRoundStart(globalTable, false);

    poolSynchronizer_->sync(globalTable.round, cs::PoolSynchronizer::roundDifferentForSync, true);

    if (conveyer.isSyncCompleted()) {
        startConsensus();
    }
    else {
        cswarning() << "NODE> non empty required hashes after BB detected";
        sendPacketHashesRequest(conveyer.currentNeededHashes(), conveyer.currentRoundNumber(), startPacketRequestPoint_);
    }
}

void Node::getRoundTableSS(const uint8_t* data, const size_t size, const cs::RoundNumber rNum) {
    cslog() << "NODE> get SS Round Table #" << rNum;
    if (cs::Conveyer::instance().currentRoundNumber() != 0) {
        csdebug() << "The RoundTable sent by SS doesn't correspond to the current RoundNumber";
        return;
    }
    istream_.init(data, size);
    cs::RoundTable roundTable;

    if (!readRoundData(roundTable, false)) {
        cserror() << "NODE> read round data from SS failed, continue without round table";
    }

    cs::Sequence lastSequence = blockChain_.getLastSeq();

    if (lastSequence >= rNum) {
        csdebug() << "NODE> remove " << lastSequence - rNum + 1 << " block(s) required (rNum = " << rNum << ", last_seq = " << lastSequence << ")";
        blockChain_.setBlocksToBeRemoved(lastSequence - rNum + 1);
    }

    // update new round data from SS
    // TODO: fix sub round
    subRound_ = 0;
    roundTable.round = rNum;

    cs::Conveyer::instance().setRound(rNum);
    cs::Conveyer::instance().setTable(roundTable);

    // "normal" start
    if (roundTable.round == 1) {
        onRoundStart(roundTable, false);
        reviewConveyerHashes();

        return;
    }

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
    istream_.init(data, size);

    cs::TransactionsPacket packet;
    istream_ >> packet;

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

    istream_.init(data, size);

    uint16_t version = 0;
    cs::Signature sig;
    istream_ >> version >> sig;
    if (!istream_.good() || !istream_.end()) {
        cswarning() << "NODE> Get stop request parsing failed";
        return;
    }
    cs::Bytes message;
    cs::DataStream stream(message);
    stream << round << version;
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

    {
        ConnectionPtr p = transport_->getConnectionByKey(cs::PacketValidator::instance().getStarterKey());
        if (p && p->isSignal) {
            if (!p->connected) {
                // strater is not connected, how to get partners in consensus?
                return false;
            }
        }
        else {
            // starter unreachable, how to get partners?
            return false;
        }
    }

    if (!critical) {
        if (Consensus::DisableTrustedRequestNextRound) {
            // ignore flag after bigbang
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
    istream_.init(data, size);

    cs::PacketsHashes hashes;
    istream_ >> hashes;

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

    istream_.init(data, size);

    cs::PacketsVector packets;
    istream_ >> packets;

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
        for (int i = 0; i < checkMask.size(); ++i) {
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

    if (!istream_.good()) {
        csmeta(cserror) << "Round info parsing failed, data is corrupted";
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
        tmpPool.add_user_field(BlockChain::TimestampID, rPackage.poolMetaInfo().timestamp);
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

void Node::createTestTransaction(int tType) {
#if 0
    csdb::Transaction transaction;

    std::string strAddr1 = "HYNKVYEC5pKAAgoJ56WdvbuzJBpjZtGBvpSMBeVd555S";
    std::string strAddr2 = "G2GeLfwjg6XuvoWnZ7ssx9EPkEBqbYL3mw3fusgpzoBk";
    std::string priv2 = "63emSYN4iwKsj9FyyZ1rzMSiGpJXtLZgArLAQz9VzTMB2i2GpnJoxREYU75K3sfEgdNnyUoyVXSQAtXUaBySXd2N";

    std::vector<uint8_t> pub_key1;
    DecodeBase58(strAddr1, pub_key1);   
    std::vector<uint8_t> pub_key2;
    DecodeBase58(strAddr2, pub_key2);

    std::vector<uint8_t> priv_key2;
    DecodeBase58(priv2, priv_key2);
    cscrypto::PrivateKey pKey2 = cscrypto::PrivateKey::readFromBytes(priv_key2);

    csdb::Address test_address1 = csdb::Address::from_public_key(pub_key1);
    csdb::Address test_address2 = csdb::Address::from_public_key(pub_key2);
    transaction.set_target(test_address1);
    transaction.set_source(test_address2);
    transaction.set_currency(csdb::Currency(1));
    transaction.set_amount(csdb::Amount(10, 0));
    transaction.set_max_fee(csdb::AmountCommission(1.0));
    transaction.add_user_field(5, tType);
    transaction.add_user_field(6, "abcde");
    transaction.set_counted_fee(csdb::AmountCommission(0.0));
    cs::TransactionsPacket transactionPack;    
    for (size_t i = 0; i < 1; ++i) {
        transaction.set_innerID((tType-1)*20);
        auto for_sig = transaction.to_byte_stream_for_sig();
        auto t1 = transaction.to_byte_stream();
        csdebug() << "Transaction for sig: " << cs::Utils::byteStreamToHex(for_sig.data(), for_sig.size());
        csdebug() << "Transaction: " << cs::Utils::byteStreamToHex(t1.data(), t1.size());
        cs::Signature sig = cscrypto::generateSignature(pKey2, for_sig.data(), for_sig.size());
        transaction.set_signature(sig);
        transactionPack.addTransaction(transaction);
    }

    transactionPack.makeHash();

    cs::Conveyer::instance().addContractPacket(transactionPack);
    csmeta(csdebug) << "NODE> Sending bad transaction's packet to all";
#else
    csunused(tType);
#endif
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

    istream_.init(data, size);
    cs::Signature sig;
    istream_ >> sig;
    cs::Bytes message;
    cs::DataStream stream(message);
    stream << rNum;
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
    cs::DataStream stream(message);

    constexpr uint8_t kEventReportVersion = 0;

    if constexpr (kEventReportVersion == 0) {
        stream << kEventReportVersion << blockChain_.getLastSeq() << bin_pack;
    }
    cs::Signature sig = cscrypto::generateSignature(solver_->getPrivateKey(), message.data(), message.size());
    ostream_.init(BaseFlags::Direct);
    ostream_ << MsgTypes::EventReport << cs::Conveyer::instance().currentRoundNumber() << sig << message;
    transport_->sendDirectToSock(ostream_.getPackets(), conf.collector_ep);
    ostream_.clear();
    csmeta(csdebug) << "event report -> " << conf.collector_ep.ip << ':' << conf.collector_ep.port;
}

void Node::getEventReport(const uint8_t* data, const std::size_t size, const cs::RoundNumber rNum, const cs::PublicKey& sender) {
    istream_.init(data, size);
    cs::Signature sig;
    cs::Bytes message;
    istream_ >> sig >> message;
    if (!cscrypto::verifySignature(sig, sender, message.data(), message.size())) {
        csdebug() << "NODE> event report from " << cs::Utils::byteStreamToHex(sender.data(), sender.size()) << " -  WRONG SIGNATURE!!!";
        return;
    }

    cs::DataStream stream(message.data(), message.size());
    uint8_t report_version = 0;
    cs::Sequence sender_last_block = 0;
    cs::Bytes bin_pack;
    stream >> report_version;
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
    cs::DataStream stream(message);
    const auto& key = contract_abs_addr.public_key();
    stream << round << key;
    cs::Signature sig = cscrypto::generateSignature(solver_->getPrivateKey(), message.data(), message.size());
    sendToList(confidants, cs::ConfidantConsts::InvalidConfidantIndex, MsgTypes::StateRequest, round, key, sig);
}

void Node::getStateRequest(const uint8_t * data, const std::size_t size, const cs::RoundNumber rNum, const cs::PublicKey & sender) {
    istream_.init(data, size);
    cs::PublicKey key;
    cs::Signature signature;
    istream_ >> key >> signature;
    csdb::Address abs_addr = csdb::Address::from_public_key(key);

    csmeta(csdebug) << cs::SmartContracts::to_base58(blockChain_, abs_addr) << " from "
        << cs::Utils::byteStreamToHex(sender.data(), sender.size());

    if (!istream_.good() || !istream_.end()) {
        cserror() << "NODE> Bad StateRequest packet format";
        return;
    }

    cs::Bytes signed_bytes;
    cs::DataStream stream(signed_bytes);
    stream << rNum << key;
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
    cs::DataStream stream(signed_data);
    const cs::PublicKey& key = contract_abs_addr.public_key();
    stream << round << key << data;
    cs::Signature sig = cscrypto::generateSignature(solver_->getPrivateKey(), signed_data.data(), signed_data.size());
    sendDirect(respondent, MsgTypes::StateReply, round, key, data, sig);
}

void Node::getStateReply(const uint8_t* data, const std::size_t size, const cs::RoundNumber rNum, const cs::PublicKey& sender) {
    istream_.init(data, size);
    cs::PublicKey key;
    cs::Bytes contract_data;
    cs::Signature signature;
    istream_ >> key >> contract_data >> signature;
    csdb::Address abs_addr = csdb::Address::from_public_key(key);
    
    csmeta(csdebug) << cs::SmartContracts::to_base58(blockChain_, abs_addr) << " from "
        << cs::Utils::byteStreamToHex(sender.data(), sender.size());

    if (!istream_.good() || !istream_.end()) {
        cserror() << "NODE> Bad State packet format";
        return;
    }

    cs::Bytes signed_data;
    cs::DataStream signed_stream(signed_data);
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
    sendToBroadcast(MsgTypes::TransactionPacket, cs::Conveyer::instance().currentRoundNumber(), packet);
}

void Node::sendPacketHashesRequest(const cs::PacketsHashes& hashes, const cs::RoundNumber round, uint32_t requestStep) {
    const cs::Conveyer& conveyer = cs::Conveyer::instance();

    if (conveyer.isSyncCompleted(round)) {
        return;
    }

    csdebug() << "NODE> Sending packet hashes request: " << hashes.size();

    if (!sendToNeighbours(MsgTypes::TransactionsPacketRequest, round, hashes)) {
        cswarning() << csname() << "Can not send packet hashes to neighbours: no neighbours";
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

    if (!sendToNeighbour(target, MsgTypes::TransactionsPacketReply, round, packets)) {
        csdebug() << "NODE> Reply transaction packets: failed send to " << cs::Utils::byteStreamToHex(target.data(), target.size());
    }
}

void Node::getBlockRequest(const uint8_t* data, const size_t size, const cs::PublicKey& sender) {
    csmeta(csdebug);

    cs::PoolsRequestedSequences sequences;

    istream_.init(data, size);
    istream_ >> sequences;

    csdebug() << "NODE> got request for " << sequences.size() << " block(s) from " << cs::Utils::byteStreamToHex(sender.data(), sender.size());

    if (sequences.empty()) {
        csmeta(cserror) << "Sequences size is 0";
        return;
    }

    std::size_t packetNum = 0;
    istream_ >> packetNum;

    if (sequences.front() > blockChain_.getLastSeq()) {
        csdebug() << "NODE> Get block request> The requested block: " << sequences.front() << " is beyond my last block";
        return;
    }

    const bool isOneBlockReply = poolSynchronizer_->isOneBlockReply();
    const std::size_t reserveSize = isOneBlockReply ? 1 : sequences.size();

    cs::PoolsBlock poolsBlock;
    poolsBlock.reserve(reserveSize);

    auto sendReply = [&] {
        sendBlockReply(poolsBlock, sender, packetNum);
        poolsBlock.clear();
    };

    for (auto& sequence : sequences) {
        csdb::Pool pool = blockChain_.loadBlock(sequence);

        if (pool.is_valid()) {
            poolsBlock.push_back(std::move(pool));

            if (isOneBlockReply) {
                sendReply();
            }
        }
        else {
            csmeta(cslog) << "unable to load block " << sequence << " from blockchain";
        }
    }

    if (!isOneBlockReply) {
        sendReply();
    }
}

void Node::getBlockReply(const uint8_t* data, const size_t size) {
    bool is_sync_on = poolSynchronizer_->isSyncroStarted();
    bool is_blockchain_uncertain = blockChain_.isLastBlockUncertain();
    if (!is_sync_on && !is_blockchain_uncertain) {
        csdebug() << "NODE> Get block reply> Pool sync has already finished";
        return;
    }

    csdebug() << "NODE> Get Block Reply";

    istream_.init(data, size);

    CompressedRegion region;
    istream_ >> region;

    size_t packetNumber = 0;
    istream_ >> packetNumber;

    cs::PoolsBlock poolsBlock = compressor_.decompress<cs::PoolsBlock>(region);

    if (poolsBlock.empty()) {
        cserror() << "NODE> Get block reply> No pools found";
        return;
    }

    if (is_blockchain_uncertain) {
        const auto last = blockChain_.getLastSeq();
        for (auto& b: poolsBlock) {
            if (b.sequence() == last) {
                cslog() << kLogPrefix_ << "get possible replacement for uncertain block " << WithDelimiters(last);
                blockChain_.storeBlock(b, true /*by_sync*/);
            }
        }
    }

    if (is_sync_on) {
        poolSynchronizer_->getBlockReply(std::move(poolsBlock), packetNumber);
    }
}

void Node::sendBlockReply(const cs::PoolsBlock& poolsBlock, const cs::PublicKey& target, std::size_t packetNum) {
    for (const auto& pool : poolsBlock) {
        csdebug() << "NODE> Send block reply. Sequence: " << pool.sequence();
    }

    csdebug() << "Node> Sending blocks with signatures:";

    for (const auto& it : poolsBlock) {
        csdebug() << "#" << it.sequence() << " signs = " << it.signatures().size();
    }

    auto region = compressor_.compress(poolsBlock);
    sendDirect(target, MsgTypes::RequestedBlock, cs::Conveyer::instance().currentRoundNumber(), region, packetNum);
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
    const auto last_seq = blockChain_.getLastSeq();
    const auto round = cs::Conveyer::instance().currentRoundNumber();
    if (stat_.isCurrentRoundTooLong(120000) && round < last_seq + cs::PoolSynchronizer::roundDifferentForSync) {
        poolSynchronizer_->syncLastPool();
    }
    else {
        poolSynchronizer_->sync(round, cs::PoolSynchronizer::roundDifferentForSync);
    }
}

void Node::addToBlackList(const cs::PublicKey& key, bool isMarked) {
    if (isMarked) {
        if (transport_->markNeighbourAsBlackListed(key)) {
            cswarning() << "Neigbour " << cs::Utils::byteStreamToHex(key) << " added to network black list";
            EventReport::sendBlackListUpdate(*this, key, true /*added*/);
        }
    }
    else {
        if (transport_->unmarkNeighbourAsBlackListed(key)) {
             cswarning() << "Neigbour " << cs::Utils::byteStreamToHex(key) << " released from network black list";
             EventReport::sendBlackListUpdate(*this, key, false /*erased*/);
        }
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

    if (myLevel_ == Level::Writer || round <= cs::TransactionsFlushRound) {
        return;
    }

    conveyer.flushTransactions();
}

void Node::onTransactionsPacketFlushed(const cs::TransactionsPacket& packet) {
    CallsQueue::instance().insert(std::bind(&Node::sendTransactionsPacket, this, packet));
}

void Node::onPingReceived(cs::Sequence sequence, const cs::PublicKey& sender) {
    static std::chrono::steady_clock::time_point point = std::chrono::steady_clock::now();
    static std::chrono::milliseconds delta{ 0 };

    auto now = std::chrono::steady_clock::now();
    delta += std::chrono::duration_cast<std::chrono::milliseconds>(now - point);

    if (maxPingSynchroDelay_ <= delta.count()) {
        auto lastSequence = blockChain_.getLastSeq();

        if (lastSequence < sequence) {
            delta = std::chrono::milliseconds(0);
            cswarning() << "Local max block " << WithDelimiters(lastSequence) << " is lower than remote one "
                << WithDelimiters(sequence) << ", trying to request round table";

            CallsQueue::instance().insert([=] {
                roundPackRequest(sender, sequence);
            });
        }
    }

    point = now;
}

void Node::sendBlockRequest(const ConnectionPtr target, const cs::PoolsRequestedSequences& sequences, std::size_t packetNum) {
    const auto round = cs::Conveyer::instance().currentRoundNumber();
    csmeta(csdetails) << "Target out(): " << target->getOut() << ", sequence from: " << sequences.front() << ", to: " << sequences.back() << ", packet: " << packetNum
                      << ", round: " << round;

    ostream_.init(BaseFlags::Direct | BaseFlags::Signed | BaseFlags::Compressed);
    ostream_ << MsgTypes::BlockRequest;
    ostream_ << round;
    ostream_ << sequences;
    ostream_ << packetNum;

    transport_->deliverDirect(ostream_.getPackets(), ostream_.getPacketsCount(), target);

    ostream_.clear();
}

void Node::sendBlockRequestToConfidants(cs::Sequence sequence) {
    cslog() << kLogPrefix_ << "request block " << WithDelimiters(sequence) << " from trusted";
    const auto round = cs::Conveyer::instance().currentRoundNumber();
    cs::PoolsRequestedSequences sequences;
    sequences.push_back(sequence);

    // try to send to confidants..
    if (sendToConfidants(MsgTypes::BlockRequest, round, sequences, size_t(0) /*packetNum*/) < Consensus::MinTrustedNodes) {
        // .. otherwise broadcast hash
        sendToBroadcast(MsgTypes::BlockRequest, round, sequences, size_t(0) /*packetNum*/);
    }
}

Node::MessageActions Node::chooseMessageAction(const cs::RoundNumber rNum, const MsgTypes type, const cs::PublicKey sender) {
    if (!good_) {
        return MessageActions::Drop;
    }

    if (poolSynchronizer_->isFastMode()) {
        if (type == MsgTypes::BlockRequest || type == MsgTypes::RequestedBlock) {
            // which round would not be on the remote we may require the requested block or get block request
            return MessageActions::Process;
        }
        else {
            return MessageActions::Drop;
        }
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
    if (type == MsgTypes::BigBang) {
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
                // TODO: roundTableRequest(cs::PublicKey respondent);
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

bool Node::isBlackListed(const cs::PublicKey pKey) {
    return transport_->isBlackListed(pKey);
}

inline bool Node::readRoundData(cs::RoundTable& roundTable, bool bang) {
    cs::PublicKey mainNode;

    uint8_t confSize = 0;
    istream_ >> confSize;

    csdebug() << "NODE> Number of confidants :" << cs::numeric_cast<int>(confSize);

    if (confSize < Consensus::MinTrustedNodes || confSize > Consensus::MaxTrustedNodes) {
        cswarning() << "Bad confidants num";
        return false;
    }

    cs::ConfidantsKeys confidants;
    confidants.reserve(confSize);

    istream_ >> mainNode;

    // TODO Fix confidants array getting (From SS)
    for (int i = 0; i < confSize; ++i) {
        cs::PublicKey key;
        istream_ >> key;

        confidants.push_back(std::move(key));
    }
    if (bang) {
        cs::Signature sig;
        istream_ >> sig;

cs::Bytes trustedToHash;
cs::DataStream tth(trustedToHash);
tth << roundTable.round;
tth << confidants;
csdebug() << "Message to Sign: " << cs::Utils::byteStreamToHex(trustedToHash);
// cs::Hash trustedHash = cscrypto::calculateHash(trustedToHash.data(), trustedToHash.size());
const auto& starter_key = cs::PacketValidator::instance().getStarterKey();
csdebug() << "SSKey: " << cs::Utils::byteStreamToHex(starter_key.data(), starter_key.size());
if (!cscrypto::verifySignature(sig, starter_key, trustedToHash.data(), trustedToHash.size())) {
    cswarning() << "The BIGBANG message is incorrect: signature isn't valid";
    return false;
}
cs::Bytes confMask;
cs::Signatures signatures;
signatures.push_back(sig);
confMask.push_back(0);
//confirmationList_.remove(roundTable.round);
confirmationList_.add(roundTable.round, bang, confidants, confMask, signatures);
    }

    if (!istream_.good() || confidants.size() < confSize) {
        cswarning() << "Bad round table format, ignoring";
        return false;
    }

    roundTable.confidants = std::move(confidants);
    //roundTable.general = mainNode;
    roundTable.hashes.clear();

    return true;
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
void Node::sendToTargetBroadcast(const cs::PublicKey& target, const MsgTypes msgType, const cs::RoundNumber round, Args&&... args) {
    static constexpr cs::Byte flags = 0; // BaseFlags::Fragmented;

    ostream_.init(flags, target);
    csdetails() << "NODE> Sending default to key: " << cs::Utils::byteStreamToHex(target.data(), target.size());

    sendToBroadcastImpl(msgType, round, std::forward<Args>(args)...);
}

template <typename... Args>
bool Node::sendToNeighbour(const cs::PublicKey& target, const MsgTypes msgType, const cs::RoundNumber round, Args&&... args) {
    ConnectionPtr connection = transport_->getConnectionByKey(target);

    if (connection) {
        sendToNeighbour(connection, msgType, round, std::forward<Args>(args)...);
    }

    return static_cast<bool>(connection);
}

template <typename... Args>
void Node::sendToNeighbour(const ConnectionPtr target, const MsgTypes msgType, const cs::RoundNumber round, Args&&... args) {
    ostream_.init(BaseFlags::Direct | /*| BaseFlags::Fragmented*/ BaseFlags::Compressed);
    ostream_ << msgType << round;

    writeDefaultStream(std::forward<Args>(args)...);

    csdetails() << "NODE> Sending Direct data: packets count: " << ostream_.getPacketsCount() << ", last packet size: " << ostream_.getCurrentSize() << ", out: " << target->out
        << ", in: " << target->in << ", specialOut: " << target->specialOut << ", msgType: " << Packet::messageTypeToString(msgType);

    transport_->deliverDirect(ostream_.getPackets(), ostream_.getPacketsCount(), target);
    ostream_.clear();
}

template <class... Args>
void Node::sendToBroadcast(const MsgTypes msgType, const cs::RoundNumber round, Args&&... args) {
    ostream_.init(BaseFlags::Broadcast /*| BaseFlags::Fragmented*/ | BaseFlags::Compressed);
    csdebug() << "NODE> Sending broadcast";

    sendToBroadcastImpl(msgType, round, std::forward<Args>(args)...);
}

template <class... Args>
bool Node::sendDirect(const cs::PublicKey& target, const MsgTypes msgType, const cs::RoundNumber round, Args&&... args) {
    if (sendToNeighbour(target, msgType, round, std::forward<Args>(args)...)) {
        return true;
    }
    if (sendToConfidant(target, msgType, round, std::forward<Args>(args)...)) {
        return true;
    }
    return false;
}

template <class... Args>
uint32_t Node::sendToConfidants(const MsgTypes msgType, const cs::RoundNumber round, Args&&... args) {
    const auto& confidants = cs::Conveyer::instance().confidants();
    const auto size = confidants.size();

    size_t i = 0;
    for (; i < size; ++i) {
        const auto& confidant = confidants.at(i);
        if (nodeIdKey_ == confidant) {
            break;
        }
    }

    if (i == size) {
        i = cs::ConfidantConsts::InvalidConfidantIndex;
    }

    return sendToList(confidants, static_cast<cs::Byte>(i), msgType, round, std::forward<Args>(args)...);
}

template <class... Args>
uint32_t Node::sendToList(const std::vector<cs::PublicKey>& listMembers, const cs::Byte listExeption, const MsgTypes msgType, const cs::RoundNumber round, Args&&... args) {
    ostream_.init(BaseFlags::Direct | BaseFlags::Compressed);
    ostream_ << msgType << round;

    writeDefaultStream(std::forward<Args>(args)...);

    csdebug() << "NODE> Sending to confidants list, size: " << ostream_.getCurrentSize() << ", round: " << round
                << ", msgType: " << Packet::messageTypeToString(msgType);

    const auto result = transport_->deliverConfidants(ostream_.getPackets(), ostream_.getPacketsCount(), listMembers, static_cast<int>(listExeption));
    ostream_.clear();

    if (!result.second.empty()) {
        std::ostringstream os;
        for (const auto item : result.second) {
            os << " [" << item << "]";
        }
        cslog() << "NODE> unable to send pack to some confidants:" << os.str().c_str();
    }
    return result.first;
}

template <class... Args>
bool Node::sendToConfidant(const cs::PublicKey& target, const MsgTypes msgType, const cs::RoundNumber round, Args&&... args) {
    if (!transport_->checkConfidant(target)) {
        return false;
    }
    uint32_t sent = sendToList(std::vector<cs::PublicKey>{target}, cs::ConfidantConsts::InvalidConfidantIndex, msgType, round, std::forward<Args>(args)...);
    return sent != 0;
}

template <typename... Args>
void Node::writeDefaultStream(Args&&... args) {
    (void)(ostream_ << ... << std::forward<Args>(args));  // fold expression
}

template <typename... Args>
bool Node::sendToNeighbours(const MsgTypes msgType, const cs::RoundNumber round, Args&&... args) {
    Connections connections;

    {
        auto lock = transport_->getNeighboursLock();
        auto neighbours = transport_->getNeighboursWithoutSS();

        if (neighbours.empty()) {
            return false;
        }

        connections = std::move(neighbours);
    }

    for (auto connection : connections) {
        sendToNeighbour(connection, msgType, round, std::forward<Args>(args)...);
    }

    return true;
}

template <typename... Args>
void Node::sendToBroadcastImpl(const MsgTypes& msgType, const cs::RoundNumber round, Args&&... args) {
    ostream_ << msgType << round;

    writeDefaultStream(std::forward<Args>(args)...);

    csdebug() << "NODE> Sending broadcast data: size: " << ostream_.getCurrentSize() << ", round: " << round
                << ", msgType: " << Packet::messageTypeToString(msgType);
    transport_->deliverBroadcast(ostream_.getPackets(), ostream_.getPacketsCount());
    ostream_.clear();
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

    sendToConfidants(MsgTypes::FirstStage, cs::Conveyer::instance().currentRoundNumber(), subRound_, stageOneInfo.signature, stageOneInfo.message);

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

    istream_.init(data, size);

    uint8_t subRound = 0;
    istream_ >> subRound;

    if (subRound != subRound_) {
        cswarning() << kLogPrefix_ << "ignore stage-1 with subround #" << static_cast<int>(subRound) << ", required #" << static_cast<int>(subRound_);
        return;
    }

    cs::StageOne stage;
    istream_ >> stage.signature;
    istream_ >> stage.message;

    if (!istream_.good() || !istream_.end()) {
        csmeta(cserror) << "Bad stage-1 packet format";
        return;
    }
    csdetails() << kLogPrefix_ << "Stage1 message: " << cs::Utils::byteStreamToHex(stage.message);
    csdetails() << kLogPrefix_ << "Stage1 signature: " << cs::Utils::byteStreamToHex(stage.signature);



    // hash of part received message
    stage.messageHash = cscrypto::calculateHash(stage.message.data(), stage.message.size());
    const cs::Conveyer& conveyer = cs::Conveyer::instance();

    cs::Bytes signedMessage;
    cs::DataStream signedStream(signedMessage);
    signedStream << conveyer.currentRoundNumber();
    signedStream << subRound_;
    signedStream << stage.messageHash;

    // stream for main message
    cs::DataStream stream(stage.message.data(), stage.message.size());
    stream >> stage.sender;
    stream >> stage.hash;
    stream >> stage.trustedCandidates;
    stream >> stage.hashesCandidates;
    stream >> stage.roundTimeStamp;

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

    sendToConfidants(MsgTypes::SecondStage, cs::Conveyer::instance().currentRoundNumber(), subRound_, stageTwoInfo.signature, stageTwoInfo.message);

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

    istream_.init(data, size);

    uint8_t subRound = 0;
    istream_ >> subRound;

    if (subRound != subRound_) {
        cswarning() << kLogPrefix_ << "Ignore StageTwo with subround #" << static_cast<int>(subRound) << ", required #" << static_cast<int>(subRound_);
        return;
    }

    cs::StageTwo stage;
    istream_ >> stage.signature;

    cs::Bytes bytes;
    istream_ >> bytes;

    if (!istream_.good() || !istream_.end()) {
        cserror() << kLogPrefix_ << "Bad StageTwo packet format";
        return;
    }

    cs::DataStream stream(bytes.data(), bytes.size());
    stream >> stage.sender;
    stream >> stage.signatures;
    stream >> stage.hashes;

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
    sendToConfidants(MsgTypes::ThirdStage, cs::Conveyer::instance().currentRoundNumber(), subRound_, stageThreeInfo.signature, stageThreeInfo.message);

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

    istream_.init(data, size);
    uint8_t subRound = 0;
    istream_ >> subRound;

    if (subRound != subRound_) {
        cswarning() << "NODE> ignore stage-3 with subround #" << static_cast<int>(subRound) << ", required #" << static_cast<int>(subRound_);
        return;
    }

    cs::StageThree stage;
    istream_ >> stage.signature;

    cs::Bytes bytes;
    istream_ >> bytes;

    if (!istream_.good() || !istream_.end()) {
        cserror() << "NODE> Bad stage-3 packet format. Packet received from: " << cs::Utils::byteStreamToHex(sender.data(), sender.size());
        return;
    }


    cs::DataStream stream(bytes.data(), bytes.size());
    stream >> stage.sender;
    stream >> stage.writer;
    stream >> stage.iteration;  // this is a potential problem!!!
    stream >> stage.blockSignature;
    stream >> stage.roundSignature;
    stream >> stage.trustedSignature;
    stream >> stage.realTrustedMask;

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

    csdebug() << kLogPrefix_ << "stageThree from T[" << static_cast<int>(stage.sender) << "] - preliminary check ... passed!";

    solver_->gotStageThree(std::move(stage), (stageThreeSent_ ? 2 : 0));
}

void Node::adjustStageThreeStorage() {
    stageThreeSent_ = false;
}

void Node::stageRequest(MsgTypes msgType, uint8_t respondent, uint8_t required, uint8_t iteration) {
    csdebug() << __func__;
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

    sendToConfidant(conveyer.confidantByIndex(respondent), msgType, cs::Conveyer::instance().currentRoundNumber(), subRound_, myConfidantIndex_, required, iteration);
    csmeta(csdetails) << "done";
}

void Node::getStageRequest(const MsgTypes msgType, const uint8_t* data, const size_t size, const cs::PublicKey& requester) {
    csdebug() << __func__;
    csmeta(csdetails) << "started";
    if (myLevel_ != Level::Confidant) {
        return;
    }

    istream_.init(data, size);

    uint8_t subRound = 0;
    istream_ >> subRound;

    if (subRound != subRound_) {
        cswarning() << kLogPrefix_ << "We got StageRequest for the Node with SUBROUND, we don't have";
        return;
    }

    uint8_t requesterNumber = 0;
    istream_ >> requesterNumber;

    uint8_t requiredNumber = 0;
    istream_ >> requiredNumber;

    uint8_t iteration = solver_->currentStage3iteration(); // default value
    if (istream_.isBytesAvailable(1)) {
        istream_ >> iteration;
    }

    if (!istream_.good() || !istream_.end()) {
        cserror() << kLogPrefix_ << "Bad StageRequest packet format";
        return;
    }

    const cs::Conveyer& conveyer = cs::Conveyer::instance();

    if (!conveyer.isConfidantExists(requesterNumber) || requester != conveyer.confidantByIndex(requesterNumber)) {
        return;
    }

    if (!conveyer.isConfidantExists(requiredNumber)) {
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
    csdebug() << __func__;
    csmeta(csdetails) << "started";

    if (myLevel_ != Level::Confidant) {
        cswarning() << kLogPrefix_ << "Only confidant nodes can send consensus stages";
        return;
    }

    const cs::Conveyer& conveyer = cs::Conveyer::instance();

    if (!conveyer.isConfidantExists(requester) || !conveyer.isConfidantExists(sender)) {
        return;
    }
    sendToConfidant(conveyer.confidantByIndex(requester), msgType, cs::Conveyer::instance().currentRoundNumber(), subRound_, signature, message);

    csmeta(csdetails) << "done";
}

void Node::sendConfidants(const std::vector<cs::PublicKey>& keys) {
    transport_->sendSSIntroduceConsensus(keys);
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
    cs::DataStream stream(data);

    stream << rejectList;

    csdebug() << "Node: sending " << rejectList.size() << " rejected contract(s) to related smart confidants";
    sendToBroadcast(MsgTypes::RejectedContracts, cs::Conveyer::instance().currentRoundNumber(), data);
}

void Node::getSmartReject(const uint8_t* data, const size_t size, const cs::RoundNumber rNum, const cs::PublicKey& sender) {
    csunused(rNum);
    csunused(sender);

    istream_.init(data, size);

    cs::Bytes bytes;
    istream_ >> bytes;

    cs::DataStream stream(bytes.data(), bytes.size());

    std::vector<RefExecution> rejectList;
    stream >> rejectList;

    if (!stream.isValid() || stream.isAvailable(1)) {
        return;
    }
    if (!istream_.good() || istream_.isBytesAvailable(1)) {
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
    //test feature
    //if (stageOneInfo.sender == 1) {
    //    cserror() << "Don't send smart stage one to all";
    //    sendToConfidant(smartConfidants[2], MsgTypes::FirstSmartStage, cs::Conveyer::instance().currentRoundNumber(),
    //           // payload
    //           stageOneInfo.message, stageOneInfo.signature);
    //}
    //else {
        sendToList(smartConfidants, stageOneInfo.sender, MsgTypes::FirstSmartStage, cs::Conveyer::instance().currentRoundNumber(),
            // payload
            stageOneInfo.message, stageOneInfo.signature);
    //}


    csmeta(csdebug) << "done";
}

void Node::getSmartStageOne(const uint8_t* data, const size_t size, const cs::RoundNumber, const cs::PublicKey& sender) {
    csdebug() << __func__ << ": starting";

    istream_.init(data, size);

    cs::StageOneSmarts stage;
    istream_ >> stage.message >> stage.signature;

    if (!istream_.good() || !istream_.end()) {
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

    cs::DataStream stream(bytes);
    stream << stageTwoInfo.sender;
    stream << stageTwoInfo.id;
    stream << stageTwoInfo.signatures;
    stream << stageTwoInfo.hashes;

    // create signature
    stageTwoInfo.signature = cscrypto::generateSignature(solver_->getPrivateKey(), bytes.data(), bytes.size());
    sendToList(smartConfidants, stageTwoInfo.sender, MsgTypes::SecondSmartStage, cs::Conveyer::instance().currentRoundNumber(), bytes, stageTwoInfo.signature);

    // cash our stage two
    stageTwoInfo.message = std::move(bytes);
    csmeta(csdebug) << "done";
}

void Node::getSmartStageTwo(const uint8_t* data, const size_t size, const cs::RoundNumber, const cs::PublicKey& sender) {
    csmeta(csdebug);

    csdebug() << "NODE> Getting SmartStage Two from " << cs::Utils::byteStreamToHex(sender.data(), sender.size());

    istream_.init(data, size);

    cs::StageTwoSmarts stage;
    istream_ >> stage.message >> stage.signature;

    if (stage.signature == cs::Zero::signature) {
        csdebug() << "NODE> Sender " << cs::Utils::byteStreamToHex(sender.data(), sender.size()) << " sent unsigned smart stage Two";
        return;
    }

    if (!istream_.good() || !istream_.end()) {
        cserror() << "NODE> Bad SmartStageTwo packet format";
        return;
    }

    cs::DataStream stream(stage.message.data(), stage.message.size());
    stream >> stage.sender;
    stream >> stage.id;
    stream >> stage.signatures;
    stream >> stage.hashes;

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

    cs::DataStream stream(bytes);
    stream << stageThreeInfo.sender;
    stream << stageThreeInfo.writer;
    stream << stageThreeInfo.id;
    stream << stageThreeInfo.realTrustedMask;
    stream << stageThreeInfo.packageSignature;

    stageThreeInfo.signature = cscrypto::generateSignature(solver_->getPrivateKey(), bytes.data(), bytes.size());
    sendToList(smartConfidants, stageThreeInfo.sender, MsgTypes::ThirdSmartStage, cs::Conveyer::instance().currentRoundNumber(),
               // payload:
              bytes, stageThreeInfo.signature);

    // cach stage three
    stageThreeInfo.message = std::move(bytes);
    csmeta(csdebug) << "done";
}

void Node::getSmartStageThree(const uint8_t* data, const size_t size, const cs::RoundNumber, const cs::PublicKey& sender) {
    csmeta(csdetails) << "started";
    csunused(sender);

    istream_.init(data, size);

    cs::StageThreeSmarts stage;
    istream_ >> stage.message >> stage.signature;

    if (stage.signature == cs::Zero::signature) {
        csdebug() << "NODE> Sender " << cs::Utils::byteStreamToHex(sender.data(), sender.size()) << " sent unsigned smart stage Three";
        return;
    }

    if (!istream_.good() || !istream_.end()) {
        cserror() << "NODE> Bad SmartStage Three packet format";
        return;
    }

    cs::DataStream stream(stage.message.data(), stage.message.size());
    stream >> stage.sender;
    stream >> stage.writer;
    stream >> stage.id;
    stream >> stage.realTrustedMask;
    stream >> stage.packageSignature;

    emit gotSmartStageThree(stage, false);
}

bool Node::smartStageRequest(MsgTypes msgType, uint64_t smartID, const cs::PublicKey& confidant, uint8_t respondent, uint8_t required) {
    csmeta(csdebug) << __func__ << "started";
    const auto res = sendToConfidant(confidant, msgType, cs::Conveyer::instance().currentRoundNumber(), smartID, respondent, required);
    csmeta(csdebug) << "done";
    return res;
}

void Node::getSmartStageRequest(const MsgTypes msgType, const uint8_t* data, const size_t size, const cs::PublicKey& requester) {
    csmeta(csdebug) << __func__ << "started";

    istream_.init(data, size);

    uint8_t requesterNumber = 0;
    uint64_t smartID = 0;
    istream_ >> smartID >> requesterNumber;

    uint8_t requiredNumber = 0;
    istream_ >> requiredNumber;

    if (!istream_.good() || !istream_.end()) {
        cserror() << "Bad SmartStage request packet format";
        return;
    }

    emit receivedSmartStageRequest(msgType, smartID, requesterNumber, requiredNumber, requester);
}

void Node::sendSmartStageReply(const cs::Bytes& message, const cs::Signature& signature, const MsgTypes msgType, const cs::PublicKey& requester) {
    csmeta(csdebug) << "started";

    sendToConfidant(requester, msgType, cs::Conveyer::instance().currentRoundNumber(), message, signature);
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

    //////////////////////////////////////////////////
    //if (std::find(lastRoundPackage->roundTable().confidants.cbegin(), lastRoundPackage->roundTable().confidants.cend(), target) != lastRoundPackage->roundTable().confidants.end()) {
    //    solver_->changeHashCollectionTimer();
    //}
    if (sendDirect(target, MsgTypes::RoundTable, rpCurrent->roundTable().round, rpCurrent->subRound(), rpCurrent->toBinary())) {
        csdebug() << "Done";
        if (!rpCurrent->poolMetaInfo().characteristic.mask.empty()) {
            csmeta(csdebug) << "Packing " << rpCurrent->poolMetaInfo().characteristic.mask.size() << " bytes of char. mask to send";
        }
        return true;
    }
    csdebug() << "Unable to send round table directly";
    return false;
}

void Node::sendRoundPackageToAll(cs::RoundPackage& rPackage) {
    // add signatures// blockSignatures, roundSignatures);
    csmeta(csdetails) << "Send round table to all";
    
    sendToBroadcast(MsgTypes::RoundTable, rPackage.roundTable().round, rPackage.subRound(), rPackage.toBinary());

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

bool Node::gotSSMessageVerify(const cs::Signature & sign, const cs::Byte* data, const size_t size)
{
    if (const auto & starter_key = cs::PacketValidator::instance().getStarterKey(); !cscrypto::verifySignature(sign, starter_key, data, size)) {
        cswarning() << "SS message is incorrect: signature isn't valid";
        csdebug() << "SSKey: " << cs::Utils::byteStreamToHex(starter_key.data(), starter_key.size());
        csdebug() << "Message to Sign: " << cs::Utils::byteStreamToHex(data, size);
        return false;
    }

    return true;
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
        //confirmationList_.add(rPackage.roundTable().round, false, currentConfidants, rPackage.poolMetaInfo().realTrustedMask, rPackage.trustedSignatures());
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

    istream_.init(data, size);

    // RoundTable evocation
    cs::Byte subRound = 0;
    istream_ >> subRound;

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
    istream_ >> bytes;

    if (!istream_.good() || !istream_.end()) {
        csmeta(cserror) << "Malformed packet with round table (1)";
        return;
    }

    cs::RoundPackage rPackage;

    if (!rPackage.fromBinary(bytes, rNum, subRound)) {
        csdebug() << "NODE> RoundPackage could not be parsed";
        return;
    }

    //if (rNum == conveyer.currentRoundNumber() + 1 && rPackage.poolMetaInfo().previousHash != blockChain_.getLastHash()) {
    //    csdebug() << "NODE> RoundPackage prevous hash is not equal to one in this node. Abort RoundPackage";
    //    return;
    //}

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

    //auto it = recdBangs.begin();
    //while (it != recdBangs.end()) {
    //    if (it->first < rPackage.roundTable().round) {
    //        it = recdBangs.erase(it);
    //        continue;
    //    }
    //    ++it;
    //}

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
    cs::DataStream stream(message);
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
    if (sendToConfidants(MsgTypes::BlockHash, round, subRound_, messageToSend, signature) < Consensus::MinTrustedNodes) {
        // .. otherwise broadcast hash
        sendToBroadcast(MsgTypes::BlockHash, round, subRound_, messageToSend, signature);
    }
    csdebug() << "NODE> Hash sent, round: " << round << "." << cs::numeric_cast<int>(subRound_) << ", message: " << cs::Utils::byteStreamToHex(messageToSend);
}

void Node::getHash(const uint8_t* data, const size_t size, cs::RoundNumber rNum, const cs::PublicKey& sender) {
    if (myLevel_ != Level::Confidant) {
        csdebug() << "NODE> ignore hash as no confidant";
        return;
    }

    csdetails() << "NODE> get hash of round " << rNum << ", data size " << size;

    istream_.init(data, size);
    uint8_t subRound = 0;
    istream_ >> subRound;

    if (subRound > subRound_) {
        cswarning() << "NODE> We got hash for the Node with SUBROUND: " << static_cast<int>(subRound) << " required #" << static_cast<int>(subRound_);
        // We don't have to return, the has of previous is the same 
    }

    cs::Bytes message;
    cs::Signature signature;
    istream_ >> message >> signature;

    if (!istream_.good() || !istream_.end()) {
        cswarning() << "NODE> bad hash packet format";
        return;
    }

    cs::StageHash sHash;
    cs::Bytes tmp;
    sHash.sender = sender;
    sHash.round = rNum;
    cs::DataStream stream(message.data(), message.size());
    stream >> tmp;
    stream >> sHash.trustedSize;
    stream >> sHash.realTrustedSize;
    stream >> sHash.timeStamp;

    if (!stream.isEmpty() || !stream.isValid()) {
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
    cs::DataStream stream1(message);
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
    /*bool notused =*/ sendDirect(respondent, MsgTypes::RoundPackRequest, round, round /*dummy data to prevent packet drop on receiver side*/);
}

void Node::askConfidantsRound(cs::RoundNumber round, const cs::ConfidantsKeys& confidants) {
    csdebug() << "NODE> ask round info #" << round << " from confidants";
    if (confidants.empty()) {
        return;
    }
    for (const auto& conf : confidants) {
        if (sendToConfidant(conf, MsgTypes::RoundPackRequest, round, round /*dummy data to prevent packet drop on receiver side*/)) {
            return;
        }
    }
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

    cs::RoundPackage& rp = roundPackageCache_.back();
    const auto currentTable = rp.roundTable();

    if (currentTable.round >= rNum) {
        if(!rp.roundSignatures().empty()) {
            if (currentTable.round == rNum) {
                ++roundPackRequests_;
            }
            if (roundPackRequests_ > currentTable.confidants.size() / 2 && roundPackRequests_ <= currentTable.confidants.size() / 2 + 1) {
                sendRoundPackageToAll(rp);
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
    cs::DataStream stream(bytes);
    stream << seq;
    cs::Signature signature = cscrypto::generateSignature(solver_->getPrivateKey(), bytes.data(), bytes.size());
    sendDirect(respondent, MsgTypes::EmptyRoundPack, seq, signature);
}

void Node::getEmptyRoundPack(const uint8_t* data, const size_t size, cs::RoundNumber rNum, const cs::PublicKey& sender) {
    csdebug() << "NODE> get empty roundPack reply from " << cs::Utils::byteStreamToHex(sender.data(), sender.size());
    istream_.init(data, size);
    cs::Signature signature;
    istream_ >> signature;
    cs::Bytes bytes;
    cs::DataStream stream(bytes);
    stream << rNum;
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
    /*bool notused =*/ sendDirect(respondent, MsgTypes::RoundTable, rp.roundTable().round, rp.subRound(), rp.toBinary());
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
    /*bool notused =*/ sendDirect(respondent, MsgTypes::RoundTableRequest, round, myConfidantIndex_);
}

void Node::getRoundTableRequest(const uint8_t* data, const size_t size, const cs::RoundNumber rNum, const cs::PublicKey& requester) {
    csmeta(csdetails) << "started, round: " << rNum;

    istream_.init(data, size);

    uint8_t requesterNumber;
    istream_ >> requesterNumber;

    if (!istream_.good() || !istream_.end()) {
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

    /*bool notused =*/ sendDirect(target, MsgTypes::RoundTableReply, cs::Conveyer::instance().currentRoundNumber(), hasRequestedInfo);
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

    istream_.init(data, size);

    bool hasRequestedInfo;
    istream_ >> hasRequestedInfo;

    if (!istream_.good() || !istream_.end()) {
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
    std::string starter_status;
    {
        ConnectionPtr p = transport_->getConnectionByKey(cs::PacketValidator::instance().getStarterKey());
        if (p && p->isSignal) {
            starter_status = (p->connected ? "connected" : "disconnected");
        }
        else {
            starter_status = "unreachable";
        }
    }
    cslog() << " Last written sequence = " << WithDelimiters(blockChain_.getLastSeq())
        << ", neighbour nodes = " << transport_->getNeighboursCountWithoutSS()
        << ", starter is " << starter_status;

    if (Transport::cntCorruptedFragments > 0 || Transport::cntDirtyAllocs > 0 || Transport::cntExtraLargeNotSent > 0) {
        cslog() << " ! " << Transport::cntDirtyAllocs << " / " << Transport::cntCorruptedFragments << " / " << Transport::cntExtraLargeNotSent;
    }

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

    istream_.init(data, size);
    uint8_t subRound = 0;
    istream_ >> subRound;

    const auto& conveyer = cs::Conveyer::instance();

    if (conveyer.currentRoundNumber() != rNum || subRound_ != subRound) {
        csdebug() << "NODE> Get hash reply on incorrect round: " << rNum << "(" << subRound << ")";
        return;
    }

    cs::Signature signature;
    istream_ >> signature;

    uint8_t senderNumber = 0;
    istream_ >> senderNumber;

    csdb::PoolHash hash;
    istream_ >> hash;

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
}


void Node::processSpecialInfo(const csdb::Pool& pool) {
    for (auto it : pool.transactions()) {
        if (getBlockChain().isSpecial(it)) {
            auto stringBytes = it.user_field(cs::trx_uf::sp::managing).value<std::string>();
            std::vector<cs::Byte> msg(stringBytes.begin(), stringBytes.end());
            cs::DataStream stream(msg.data(), msg.size());
            uint16_t order;
            stream >> order;
            if (order == 2U) {
                uint8_t cnt;
                stream >> cnt;
                csdebug() << "New bootstrap nodes: ";
                for (uint8_t i = 0; i < cnt; ++i) {
                    cs::PublicKey key;
                    stream >> key;
                    bootStrapNodes_.push_back(key);
                    csdebug() << static_cast<int>(i) << ". " << cs::Utils::byteStreamToHex(key);
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
    
    constexpr const uint64_t uuidTestNet = 5283967947175248524;
    constexpr const uint64_t uuidMainNet = 11024959585341937636;
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
    cslog() << "Waiting for next round...";
}
