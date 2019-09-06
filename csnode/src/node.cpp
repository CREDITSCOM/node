#include <algorithm>
#include <csignal>
#include <numeric>
#include <sstream>

#include <solver/consensus.hpp>
#include <solver/solvercore.hpp>
#include <solver/smartcontracts.hpp>

#include <csnode/conveyer.hpp>
#include <csnode/datastream.hpp>
#include <csnode/node.hpp>
#include <csnode/nodecore.hpp>
#include <csnode/nodeutils.hpp>
#include <csnode/poolsynchronizer.hpp>
#include <csnode/blockvalidator.hpp>
#include <csnode/roundpackage.hpp>

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

Node::Node(const Config& config, cs::config::Observer& observer)
: nodeIdKey_(config.getMyPublicKey())
, nodeIdPrivate_(config.getMyPrivateKey())
, blockChain_(genesisAddress_, startAddress_, config.recreateIndex())
, ostream_(&packStreamAllocator_, nodeIdKey_)
, stat_()
, blockValidator_(std::make_unique<cs::BlockValidator>(*this))
, observer_(observer) {
    solver_ = new cs::SolverCore(this, genesisAddress_, startAddress_);
    std::cout << "Start transport... ";
    transport_ = new Transport(config, this);
    std::cout << "Done\n";
    poolSynchronizer_ = new cs::PoolSynchronizer(config.getPoolSyncSettings(), transport_, &blockChain_);

    executor::ExecutorSettings::set(cs::makeReference(blockChain_),
                                    cs::makeReference(solver_),
                                    cs::makeReference(config));

    auto& executor = executor::Executor::getInstance();

    cs::Connector::connect(&blockChain_.readBlockEvent(), &stat_, &cs::RoundStat::onReadBlock);
    cs::Connector::connect(&blockChain_.storeBlockEvent, &stat_, &cs::RoundStat::onStoreBlock);
    cs::Connector::connect(&blockChain_.storeBlockEvent, &executor, &executor::Executor::onBlockStored);
    cs::Connector::connect(&blockChain_.readBlockEvent(), &executor, &executor::Executor::onReadBlock);
    cs::Connector::connect(&transport_->pingReceived, this, &Node::onPingReceived);
    cs::Connector::connect(&Node::stopRequested, this, &Node::onStopRequested);
    cs::Connector::connect(&blockChain_.readBlockEvent(), this, &Node::validateBlock);

    setupObserver();

    alwaysExecuteContracts_ = config.alwaysExecuteContracts();
    good_ = init(config);
}

Node::~Node() {
    std::cout << "Desturctor called\n";

    sendingTimer_.stop();

    delete solver_;
    delete transport_;
    delete poolSynchronizer_;
}

bool Node::init(const Config& config) {
#ifdef NODE_API
    std::cout << "Init API... ";

    api_ = std::make_unique<csconnector::connector>(blockChain_, solver_, config);

    std::cout << "Done\n";

    cs::Connector::connect(&blockChain_.readBlockEvent(), api_.get(), &csconnector::connector::onReadFromDB);
    cs::Connector::connect(&blockChain_.storeBlockEvent, api_.get(), &csconnector::connector::onStoreBlock);

#endif  // NODE_API

    // must call prior to blockChain_.init():
    solver_->init(nodeIdKey_, nodeIdPrivate_);
    solver_->startDefault();

    if (!blockChain_.init(config.getPathToDB())) {
        return false;
    }

    cslog() << "Blockchain is ready, contains " << WithDelimiters(stat_.total_transactions()) << " transactions";

#ifdef NODE_API
    api_->run();
#endif  // NODE_API

    if (!transport_->isGood()) {
        return false;
    }

    std::cout << "Transport is init\n";

    if (!solver_) {
        return false;
    }

    std::cout << "Solver is init\n";
    std::cout << "Everything is init\n";

    cs::Conveyer::instance().setData(config.conveyerData());

    cs::Connector::connect(&sendingTimer_.timeOut, this, &Node::processTimer);
    cs::Connector::connect(&cs::Conveyer::instance().packetFlushed, this, &Node::onTransactionsPacketFlushed);
    cs::Connector::connect(&poolSynchronizer_->sendRequest, this, &Node::sendBlockRequest);

    initCurrentRP();
    maxNeighboursSequence_ = blockChain_.getLastSeq();

    return true;
}

void Node::setupObserver() {
    // connect config observer to entities
    cs::Connector::connect(&observer_.configChanged, &cs::Conveyer::instance(), &cs::Conveyer::onConfigChanged);
    cs::Connector::connect(&observer_.configChanged, &executor::Executor::getInstance(), &executor::Executor::onConfigChanged);
    cs::Connector::connect(&observer_.configChanged, transport_, &Transport::onConfigChanged);
}

void Node::run() {
    std::cout << "Running transport\n";
    transport_->run();
}

void Node::stop() {
    good_ = false;

    transport_->stop();
    cswarning() << "[TRANSPORT STOPPED]";

    solver_->finish();
    cswarning() << "[SOLVER STOPPED]";

    blockChain_.close();

    if (api_) {
        api_->apiExecHandler()->getExecutor().stop();
        cswarning() << "[EXECUTOR IS SIGNALED TO STOP]";
    }

    cswarning() << "[BLOCKCHAIN STORAGE CLOSED]";

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

void Node::getBigBang(const uint8_t* data, const size_t size, const cs::RoundNumber rNum) {
    auto& conveyer = cs::Conveyer::instance();

    cswarning() << "-----------------------------------------------------------";
    cswarning() << "NODE> BigBang #" << rNum << ": last written #" << blockChain_.getLastSeq() << ", current #" << conveyer.currentRoundNumber();
    cswarning() << "-----------------------------------------------------------";
    istream_.init(data, size);
    uint8_t tmp = 0;
    istream_ >> tmp;
    if (tmp <= recdBangs[rNum] && !(tmp < Consensus::MaxSubroundDelta && recdBangs[rNum] > std::numeric_limits<uint8_t>::max() - Consensus::MaxSubroundDelta)) {
        cswarning() << "Old Big Bang received: " << rNum << "." << static_cast<int>(tmp) << " is <= " << rNum << "." << static_cast<int>(recdBangs[rNum]);
        return;
    }
    subRound_ = tmp;

    // cache
    auto cachedRound = conveyer.currentRoundNumber();

    // update round data
    recdBangs[rNum] = subRound_;

    cs::Hash lastBlockHash;
    istream_ >> lastBlockHash;

    cs::RoundTable globalTable;
    globalTable.round = rNum;

    if (!readRoundData(globalTable, true)) {
        cserror() << className() << " read round data from SS failed";
        return;
    }

    solver_->resetGrayList();

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

    maxNeighboursSequence_ = globalTable.round;

    conveyer.updateRoundTable(cachedRound, globalTable);
    onRoundStart(globalTable);

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
    istream_.init(data, size);
    if (cs::Conveyer::instance().currentRoundNumber() != 0) {
        csdebug() << "The RoundTable sent by SS doesn't correspond to the current RoundNumber";
        return;
    }
    cslog() << "NODE> get SS Round Table #" << rNum;

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

    if (maxNeighboursSequence_ < rNum) {
        maxNeighboursSequence_ = rNum;
    }

    cs::Conveyer::instance().setRound(rNum);
    cs::Conveyer::instance().setTable(roundTable);

    // "normal" start
    if (roundTable.round == 1) {
        onRoundStart(roundTable);
        reviewConveyerHashes();

        return;
    }

    poolSynchronizer_->sync(rNum);
}

void Node::getTransactionsPacket(const uint8_t* data, const std::size_t size) {
    istream_.init(data, size);
    cs::TransactionsPacket packet;
    istream_ >> packet;

    if (packet.hash().isEmpty()) {
        cswarning() << "Received transaction packet hash is empty";
        return;
    }

    processTransactionsPacket(std::move(packet));
}

void Node::getNodeStopRequest(const cs::RoundNumber round, const uint8_t* data, const std::size_t size) {
    const auto localRound = cs::Conveyer::instance().currentRoundNumber();

    if (round < localRound && localRound - round > cs::MaxRoundDeltaInStopRequest) {
        // ignore too aged command to prevent store & re-use by enemies
        return;
    }

    istream_.init(data, size);

    uint16_t version = 0;
    istream_ >> version;

    if (!istream_.good() || istream_.remainsBytes() != cscrypto::kSignatureSize) {
        cswarning() << "NODE> Get stop request parsing failed";
        return;
    }

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

    if (wData.balance_ < Consensus::MinStakeValue) {
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

    csdebug() << "NODE> Get packet hashes request from " << cs::Utils::byteStreamToHex(sender.data(), sender.size());
    csdebug() << "NODE> Requested packet hashes: " << hashes.size();

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

    cs::Packets packets;
    istream_ >> packets;

    if (packets.empty()) {
        csmeta(cserror) << "Packet hashes reply, bad packets parsing";
        return;
    }

    csdebug() << "NODE> Get packet hashes reply: sender " << cs::Utils::byteStreamToHex(sender);
    csdebug() << "NODE> Hashes reply got packets count: " << packets.size();

    processPacketsReply(std::move(packets), round);
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

    //for (size_t idx = 0; idx < realTrustedMaskSize; ++idx) {
    //    const auto& key = confidantsReference[idx];

    //    if (rPackage.poolMetaInfo().realTrustedMask[idx] == 0) {
    //        poolMetaInfo.writerKey = key;
    //    }
    //}

    //if (round != 0) {
    //    auto confirmation = confirmationList_.find(round);
    //    if (confirmation.has_value()) {
    //        poolMetaInfo.confirmationMask = confirmation.value().mask;
    //        poolMetaInfo.confirmations = confirmation.value().signatures;
    //    }
    //}

    if (!istream_.good()) {
        csmeta(cserror) << "Round info parsing failed, data is corrupted";
        return;
    }

    csdebug() << "NODE> Sequence " << rPackage.poolMetaInfo().sequenceNumber << ", mask size " << rPackage.poolMetaInfo().characteristic.mask.size();
    csdebug() << "NODE> Time: " << rPackage.poolMetaInfo().timestamp;

    if (blockChain_.getLastSeq() > rPackage.poolMetaInfo().sequenceNumber) {
        csmeta(cswarning) << "blockChain last seq: " << blockChain_.getLastSeq() 
            << " > pool meta info seq: " << rPackage.poolMetaInfo().sequenceNumber;
        return;
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
        //confirmationList_.remove(round);
    }

    csmeta(csdetails) << "done";
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
    sendDefault(respondent, MsgTypes::StateReply, round, key, data, sig);
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

    sendBroadcast(MsgTypes::TransactionPacket, cs::Conveyer::instance().currentRoundNumber(), packet);
}

void Node::sendPacketHashesRequest(const cs::PacketsHashes& hashes, const cs::RoundNumber round, uint32_t requestStep) {
    const cs::Conveyer& conveyer = cs::Conveyer::instance();

    if (conveyer.isSyncCompleted(round)) {
        return;
    }

    csdebug() << "NODE> Sending packet hashes request: " << hashes.size();

    cs::PublicKey main;
    const auto msgType = MsgTypes::TransactionsPacketRequest;
    const auto roundTable = conveyer.roundTable(round);

    // look at main node
    main = (roundTable != nullptr) ? roundTable->confidants.front(): conveyer.currentRoundTable().confidants.front();

    const bool sendToGeneral = sendToNeighbour(main, msgType, round, hashes);

    if (!sendToGeneral) {
        sendPacketHashesRequestToRandomNeighbour(hashes, round);
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

void Node::sendPacketHashesRequestToRandomNeighbour(const cs::PacketsHashes& hashes, const cs::RoundNumber round) {
    const auto msgType = MsgTypes::TransactionsPacketRequest;
    const auto neighboursCount = transport_->getNeighboursCount();

    bool successRequest = false;

    for (std::size_t i = 0; i < neighboursCount; ++i) {
        ConnectionPtr connection = transport_->getConnectionByNumber(i);

        if (connection && !connection->isSignal) {
            successRequest = true;
            sendToNeighbour(connection, msgType, round, hashes);
        }
    }

    if (!successRequest) {
        csdebug() << "NODE> Send broadcast hashes request, no neigbours";
        sendBroadcast(msgType, round, hashes);
        return;
    }

    csdebug() << "NODE> Send hashes request to all neigbours";
}

void Node::sendPacketHashesReply(const cs::Packets& packets, const cs::RoundNumber round, const cs::PublicKey& target) {
    if (packets.empty()) {
        return;
    }

    csdebug() << "NODE> Reply transaction packets: " << packets.size();

    const auto msgType = MsgTypes::TransactionsPacketReply;
    const bool success = sendToNeighbour(target, msgType, round, packets);

    if (!success) {
        csdebug() << "NODE> Reply transaction packets: failed send to " << cs::Utils::byteStreamToHex(target.data(), target.size()) << ", perform broadcast";
        sendBroadcast(target, msgType, round, packets);
    }
}

void Node::getBlockRequest(const uint8_t* data, const size_t size, const cs::PublicKey& sender) {
    csmeta(csdebug);

    cs::PoolsRequestedSequences sequences;

    istream_.init(data, size);
    istream_ >> sequences;

    csdebug() << "NODE> Block request got sequences count: " << sequences.size();
    csdebug() << "NODE> Get packet hashes request: sender " << cs::Utils::byteStreamToHex(sender.data(), sender.size());

    if (sequences.empty()) {
        csmeta(cserror) << "Sequences size is 0";
        return;
    }

    std::size_t packetNum = 0;
    istream_ >> packetNum;

    csdebug() << "NODE> Get block request> Getting the request for block: from: " << sequences.front() << ", to: " << sequences.back() << ", id: " << packetNum;

    if (sequences.front() > blockChain_.getLastSeq()) {
        csdebug() << "NODE> Get block request> The requested block: " << sequences.front() << " is beyond last written sequence";
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
            csmeta(cserror) << "Load block: " << sequence << " from blockchain is Invalid";
        }
    }

    if (!isOneBlockReply) {
        sendReply();
    }
}

void Node::getBlockReply(const uint8_t* data, const size_t size) {
    if (!poolSynchronizer_->isSyncroStarted()) {
        csdebug() << "NODE> Get block reply> Pool synchronizer already syncro";
        return;
    }

    csdebug() << "NODE> Get Block Reply";

    cs::PoolsBlock poolsBlock = decompressPoolsBlock(data, size);

    if (poolsBlock.empty()) {
        cserror() << "NODE> Get block reply> Pools count is 0";
        return;
    }

    for (const auto& pool : poolsBlock) {
        transport_->syncReplied(pool.sequence());
    }

    std::size_t packetNum = 0;
    istream_ >> packetNum;

    poolSynchronizer_->getBlockReply(std::move(poolsBlock), packetNum);
}

void Node::sendBlockReply(const cs::PoolsBlock& poolsBlock, const cs::PublicKey& target, std::size_t packetNum) {
    for (const auto& pool : poolsBlock) {
        csdebug() << "NODE> Send block reply. Sequence: " << pool.sequence();
    }

    csdebug() << "Node> Sending blocks with signatures:";
    for (const auto& it : poolsBlock) {
        csdebug() << "#" << it.sequence() << " signs = " << it.signatures().size();
    }

    std::size_t realBinSize = 0;
    RegionPtr memPtr = compressPoolsBlock(poolsBlock, realBinSize);

    tryToSendDirect(target, MsgTypes::RequestedBlock, cs::Conveyer::instance().currentRoundNumber(), realBinSize, cs::numeric_cast<uint32_t>(memPtr->size()), memPtr, packetNum);
}

void Node::becomeWriter() {
    myLevel_ = Level::Writer;
    csdebug() << "NODE> Became writer";
}

void Node::processPacketsRequest(cs::PacketsHashes&& hashes, const cs::RoundNumber round, const cs::PublicKey& sender) {
    csdebug() << "NODE> Processing packets sync request";

    cs::Packets packets;

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

void Node::processPacketsReply(cs::Packets&& packets, const cs::RoundNumber round) {
    csdebug() << "NODE> Processing packets reply";

    cs::Conveyer& conveyer = cs::Conveyer::instance();

    for (auto&& packet : packets) {
        conveyer.addFoundPacket(round, std::move(packet));
    }

    if (conveyer.isSyncCompleted(round)) {
        csdebug() << "NODE> Packets sync completed, #" << round;
        transport_->resetNeighbours();

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
    static std::chrono::milliseconds delta{0};

    auto now = std::chrono::steady_clock::now();
    delta += std::chrono::duration_cast<std::chrono::milliseconds>(now - point);

    if (sequence <= maxNeighboursSequence_) {
        delta = std::chrono::milliseconds(0);
    }

    if (maxPingSynchroDelay_ <= delta.count()) {
        delta = std::chrono::milliseconds(0);
        auto lastSequence = blockChain_.getLastSeq();

        if (lastSequence < sequence) {
            cswarning() << "Last sequence is lower than network max sequence, trying to request round table";

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

    ostream_.init(BaseFlags::Neighbours | BaseFlags::Signed | BaseFlags::Compressed);
    ostream_ << MsgTypes::BlockRequest;
    ostream_ << round;
    ostream_ << sequences;
    ostream_ << packetNum;

    transport_->deliverDirect(ostream_.getPackets(), ostream_.getPacketsCount(), target);

    ostream_.clear();
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
            return MessageActions::Process;

        default:
            break;
    }

    const auto round = cs::Conveyer::instance().currentRoundNumber();

    // starts next round, otherwise
    if (type == MsgTypes::RoundTable) {
        if (rNum > round) {
            return MessageActions::Process;
        }

        // TODO: detect absence of proper current round info (round may be set by SS or BB)
        return MessageActions::Drop;
    }

    // BB: every round (for now) may be handled:
    if (type == MsgTypes::BigBang) {
        return MessageActions::Process;
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
void Node::sendDefault(const cs::PublicKey& target, const MsgTypes msgType, const cs::RoundNumber round, Args&&... args) {
    static constexpr cs::Byte defautFlags = 0; // BaseFlags::Fragmented;

    ostream_.init(defautFlags, target);
    csdetails() << "NODE> Sending default to key: " << cs::Utils::byteStreamToHex(target.data(), target.size());

    sendBroadcastImpl(msgType, round, std::forward<Args>(args)...);
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
    ostream_.init(BaseFlags::Neighbours | BaseFlags::Broadcast /*| BaseFlags::Fragmented*/ | BaseFlags::Compressed);
    ostream_ << msgType << round;

    writeDefaultStream(std::forward<Args>(args)...);

    csdetails() << "NODE> Sending Direct data: packets count: " << ostream_.getPacketsCount() << ", last packet size: " << ostream_.getCurrentSize() << ", out: " << target->out
                << ", in: " << target->in << ", specialOut: " << target->specialOut << ", msgType: " << Packet::messageTypeToString(msgType);

    transport_->deliverDirect(ostream_.getPackets(), ostream_.getPacketsCount(), target);
    ostream_.clear();
}

template <class... Args>
void Node::sendBroadcast(const MsgTypes msgType, const cs::RoundNumber round, Args&&... args) {
    ostream_.init(BaseFlags::Broadcast /*| BaseFlags::Fragmented*/ | BaseFlags::Compressed);
    csdetails() << "NODE> Sending broadcast";

    sendBroadcastImpl(msgType, round, std::forward<Args>(args)...);
}

template <class... Args>
void Node::tryToSendDirect(const cs::PublicKey& target, const MsgTypes msgType, const cs::RoundNumber round, Args&&... args) {
    const bool success = sendToNeighbour(target, msgType, round, std::forward<Args>(args)...);
    if (!success) {
        sendBroadcast(target, msgType, round, std::forward<Args>(args)...);
    }
}

template <class... Args>
bool Node::sendToRandomNeighbour(const MsgTypes msgType, const cs::RoundNumber round, Args&&... args) {
    return transport_->forRandomNeighbour([&, this](const auto target){
        sendToNeighbour(target, msgType, round, std::forward<Args>(args)...);
    });
}

template <class... Args>
void Node::sendToConfidants(const MsgTypes msgType, const cs::RoundNumber round, Args&&... args) {
    const auto& confidants = cs::Conveyer::instance().confidants();
    const auto size = confidants.size();

    for (size_t i = 0; i < size; ++i) {
        const auto& confidant = confidants.at(i);

        if (myConfidantIndex_ == i && nodeIdKey_ == confidant) {
            continue;
        }

        sendBroadcast(confidant, msgType, round, std::forward<Args>(args)...);
    }
}

template <class... Args>
void Node::sendToList(const std::vector<cs::PublicKey>& listMembers, const cs::Byte listExeption, const MsgTypes msgType, const cs::RoundNumber round, Args&&... args) {
    const auto size = listMembers.size();

    for (size_t i = 0; i < size; ++i) {
        const auto& listMember = listMembers[i];

        if (listExeption == i && nodeIdKey_ == listMember) {
            continue;
        }

        sendBroadcast(listMember, msgType, round, std::forward<Args>(args)...);
    }
}

template <typename... Args>
void Node::writeDefaultStream(Args&&... args) {
    (void)(ostream_ << ... << std::forward<Args>(args));  // fold expression
}

template <typename... Args>
bool Node::sendToNeighbours(const MsgTypes msgType, const cs::RoundNumber round, Args&&... args) {
    auto lock = transport_->getNeighboursLock();
    Connections connections = transport_->getNeighboursWithoutSS();

    if (connections.empty()) {
        return false;
    }

    for (auto connection : connections) {
        sendToNeighbour(connection, msgType, round, std::forward<Args>(args)...);
    }
}

template <typename... Args>
void Node::sendBroadcast(const cs::PublicKey& target, const MsgTypes& msgType, const cs::RoundNumber round, Args&&... args) {
    ostream_.init(BaseFlags::Fragmented | BaseFlags::Compressed, target);
    csdetails() << "NODE> Sending broadcast to key: " << cs::Utils::byteStreamToHex(target.data(), target.size());

    sendBroadcastImpl(msgType, round, std::forward<Args>(args)...);
}

template <typename... Args>
void Node::sendBroadcastImpl(const MsgTypes& msgType, const cs::RoundNumber round, Args&&... args) {
    ostream_ << msgType << round;

    writeDefaultStream(std::forward<Args>(args)...);

    csdetails() << "NODE> Sending broadcast data: size: " << ostream_.getCurrentSize() << ", last packet size: " << ostream_.getCurrentSize() << ", round: " << round
                << ", msgType: " << Packet::messageTypeToString(msgType);
	//if (ostream_.getPacketsCount() > 100) {
	//	csinfo() << __func__ << ": sending " << ostream_.getPacketsCount() << " packets";
	//}
    transport_->deliverBroadcast(ostream_.getPackets(), ostream_.getPacketsCount());
    ostream_.clear();
}

RegionPtr Node::compressPoolsBlock(const cs::PoolsBlock& poolsBlock, std::size_t& realBinSize) {
    cs::Bytes bytes;
    cs::DataStream stream(bytes);

    stream << poolsBlock;

    char* data = reinterpret_cast<char*>(bytes.data());
    const int binSize = cs::numeric_cast<int>(bytes.size());

    const auto maxSize = LZ4_compressBound(binSize);
    auto memPtr = allocator_.allocateNext(static_cast<uint32_t>(maxSize));

    const int compressedSize = LZ4_compress_default(data, static_cast<char*>(memPtr->data()), binSize, cs::numeric_cast<int>(memPtr->size()));

    if (!compressedSize) {
        csmeta(cserror) << "Compress poools block error";
    }

    memPtr->setSize(static_cast<uint32_t>(compressedSize));
    realBinSize = cs::numeric_cast<std::size_t>(binSize);

    return memPtr;
}

cs::PoolsBlock Node::decompressPoolsBlock(const uint8_t* data, const size_t size) {
    istream_.init(data, size);
    std::size_t realBinSize = 0;
    istream_ >> realBinSize;

    std::uint32_t compressSize = 0;
    istream_ >> compressSize;

    RegionPtr memPtr = allocator_.allocateNext(compressSize);
    istream_ >> memPtr;

    cs::Bytes bytes;
    bytes.resize(realBinSize);
    char* bytesData = reinterpret_cast<char*>(bytes.data());

    const int uncompressedSize = LZ4_decompress_safe(static_cast<char*>(memPtr->data()), bytesData, cs::numeric_cast<int>(compressSize), cs::numeric_cast<int>(realBinSize));

    if (uncompressedSize < 0) {
        csmeta(cserror) << "Decompress poools block error";
    }

    cs::DataStream stream(bytes.data(), bytes.size());
    cs::PoolsBlock poolsBlock;

    stream >> poolsBlock;

    return poolsBlock;
}

void Node::sendStageOne(const cs::StageOne& stageOneInfo) {
    if (myLevel_ != Level::Confidant) {
        cswarning() << "NODE> Only confidant nodes can send consensus stages";
        return;
    }

    csmeta(csdebug) << "Round: " << cs::Conveyer::instance().currentRoundNumber() << "." << cs::numeric_cast<int>(subRound_)
        << cs::StageOne::toString(stageOneInfo);

    csdebug() << "Stage one Message R-" << cs::Conveyer::instance().currentRoundNumber() << "[" << static_cast<int>(stageOneInfo.sender)
        << "]: " << cs::Utils::byteStreamToHex(stageOneInfo.messageBytes.data(), stageOneInfo.messageBytes.size());
    csdebug() << "Stage one Signature R-" << cs::Conveyer::instance().currentRoundNumber() << "[" << static_cast<int>(stageOneInfo.sender)
        << "]: " << cs::Utils::byteStreamToHex(stageOneInfo.signature.data(), stageOneInfo.signature.size());

    sendToConfidants(MsgTypes::FirstStage, cs::Conveyer::instance().currentRoundNumber(), subRound_, stageOneInfo.signature, stageOneInfo.messageBytes);

    csmeta(csdetails) << "Sent message size " << stageOneInfo.messageBytes.size();
}

void Node::getStageOne(const uint8_t* data, const size_t size, const cs::PublicKey& sender) {
    csmeta(csdetails) << "started";

    if (myLevel_ != Level::Confidant) {
        csdebug() << "NODE> ignore stage-1 as no confidant";
        return;
    }

    istream_.init(data, size);

    uint8_t subRound;
    istream_ >> subRound;

    if (subRound != subRound_) {
        cswarning() << "NODE> ignore stage-1 with subround #" << subRound << ", required #" << subRound_;
        return;
    }

    cs::StageOne stage;
    istream_ >> stage.signature;
    istream_ >> stage.messageBytes;

    if (!istream_.good() || !istream_.end()) {
        csmeta(cserror) << "Bad stage-1 packet format";
        return;
    }
    csdetails() << "Stage1 message: " << cs::Utils::byteStreamToHex(stage.messageBytes);
    csdetails() << "Stage1 signature: " << cs::Utils::byteStreamToHex(stage.signature);



    // hash of part received message
    stage.messageHash = cscrypto::calculateHash(stage.messageBytes.data(), stage.messageBytes.size());
    const cs::Conveyer& conveyer = cs::Conveyer::instance();

    cs::Bytes signedMessage;
    cs::DataStream signedStream(signedMessage);
    signedStream << conveyer.currentRoundNumber();
    signedStream << subRound_;
    signedStream << stage.messageHash;

    // stream for main message
    cs::DataStream stream(stage.messageBytes.data(), stage.messageBytes.size());
    stream >> stage.sender;
    stream >> stage.hash;
    stream >> stage.trustedCandidates;
    stream >> stage.hashesCandidates;
    stream >> stage.roundTimeStamp;

    if (!conveyer.isConfidantExists(stage.sender)) {
        return;
    }

    const cs::PublicKey& confidant = conveyer.confidantByIndex(stage.sender);
    csdebug() << "StageMessage[" << static_cast<int>(stage.sender) <<"]: " << cs::Utils::byteStreamToHex(stage.messageBytes.data(), stage.messageBytes.size());
    if (!cscrypto::verifySignature(stage.signature, confidant, signedMessage.data(), signedMessage.size())) {
        cswarning() << "NODE> Stage-1 from T[" << static_cast<int>(stage.sender) << "] -  WRONG SIGNATURE!!!";
        return;
    }

    if (confidant != sender) {
        csmeta(csdebug) << "Stage-1 of " << getSenderText(confidant) << " sent by " << getSenderText(sender);
    }
    else {
        csmeta(csdebug) << "Stage-1 of T[" << static_cast<int>(stage.sender) << "], sender key ok";
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

    sendToConfidants(MsgTypes::SecondStage, cs::Conveyer::instance().currentRoundNumber(), subRound_, stageTwoInfo.signature, stageTwoInfo.messageBytes);

    // cash our stage two
    csmeta(csdetails) << "Bytes size " << stageTwoInfo.messageBytes.size() << " ... done";
}

void Node::getStageTwo(const uint8_t* data, const size_t size, const cs::PublicKey& sender) {
    csmeta(csdetails);

    if (myLevel_ != Level::Confidant && myLevel_ != Level::Writer) {
        csdebug() << "NODE> ignore stage-2 as no confidant";
        return;
    }

    csdebug() << "NODE> getting stage-2 from " << getSenderText(sender);

    istream_.init(data, size);

    uint8_t subRound = 0;
    istream_ >> subRound;

    if (subRound != subRound_) {
        cswarning() << "NODE> We got Stage-2 for the Node with SUBROUND, we don't have";
        return;
    }

    cs::StageTwo stage;
    istream_ >> stage.signature;

    cs::Bytes bytes;
    istream_ >> bytes;

    if (!istream_.good() || !istream_.end()) {
        cserror() << "NODE> Bad stage-2 packet format";
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
        csdebug() << "NODE> stage-2 [" << static_cast<int>(stage.sender) << "] -  WRONG SIGNATURE!!!";
        return;
    }

    csmeta(csdetails) << "Signature is OK";
    stage.messageBytes = std::move(bytes);

    csdebug() << "NODE> stage-2 [" << static_cast<int>(stage.sender) << "] is OK!";
    solver_->gotStageTwo(stage);
}

void Node::sendStageThree(cs::StageThree& stageThreeInfo) {
    csdebug() << __func__;

    if (myLevel_ != Level::Confidant) {
        cswarning() << "NODE> Only confidant nodes can send consensus stages";
        return;
    }

    // TODO: think how to improve this code

    sendToConfidants(MsgTypes::ThirdStage, cs::Conveyer::instance().currentRoundNumber(), subRound_, stageThreeInfo.signature, stageThreeInfo.messageBytes);

    // cach stage three
    csmeta(csdetails) << "bytes size " << stageThreeInfo.messageBytes.size();
    stageThreeSent_ = true;
    csmeta(csdetails) << "done";
}

void Node::getStageThree(const uint8_t* data, const size_t size) {
    csmeta(csdetails);

    if (myLevel_ != Level::Confidant && myLevel_ != Level::Writer) {
        csdebug() << "NODE> ignore stage-3 as no confidant";
        return;
    }

    istream_.init(data, size);
    uint8_t subRound = 0;
    istream_ >> subRound;

    if (subRound != subRound_) {
        cswarning() << "NODE> We got Stage-2 for the Node with SUBROUND, we don't have";
        return;
    }

    cs::StageThree stage;
    istream_ >> stage.signature;

    cs::Bytes bytes;
    istream_ >> bytes;

    if (!istream_.good() || !istream_.end()) {
        cserror() << "NODE> Bad stage-3 packet format";
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
        stageRequest(MsgTypes::ThirdStage, myConfidantIndex_, stage.sender);
        return;
    }
    else if (stage.iteration > solver_->currentStage3iteration()) {
        // store
        return;
    }

    if (!cscrypto::verifySignature(stage.signature, conveyer.confidantByIndex(stage.sender), bytes.data(), bytes.size())) {
        cswarning() << "NODE> stage-3 from T[" << static_cast<int>(stage.sender) << "] -  WRONG SIGNATURE!!!";
        return;
    }

    stage.messageBytes = std::move(bytes);

    csdebug() << "NODE> stage-3 from T[" << static_cast<int>(stage.sender) << "] - preliminary check ... passed!";

    solver_->gotStageThree(std::move(stage), (stageThreeSent_ ? 2 : 0));
}

void Node::adjustStageThreeStorage() {
    stageThreeSent_ = false;
}

void Node::stageRequest(MsgTypes msgType, uint8_t respondent, uint8_t required /*, uint8_t iteration*/) {
    csdebug() << __func__;
    if (myLevel_ != Level::Confidant && myLevel_ != Level::Writer) {
        cswarning() << "NODE> Only confidant nodes can request consensus stages";
        return;
    }

    const cs::Conveyer& conveyer = cs::Conveyer::instance();

    if (!conveyer.isConfidantExists(respondent)) {
        return;
    }

    sendDefault(conveyer.confidantByIndex(respondent), msgType, cs::Conveyer::instance().currentRoundNumber(), subRound_, myConfidantIndex_, required /*, iteration*/);
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
        cswarning() << "NODE> We got Stage-2 for the Node with SUBROUND, we don't have";
        return;
    }

    uint8_t requesterNumber = 0;
    istream_ >> requesterNumber;

    uint8_t requiredNumber = 0;
    istream_ >> requiredNumber;

    if (!istream_.good() || !istream_.end()) {
        cserror() << "Bad StageThree packet format";
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
            solver_->gotStageThreeRequest(requesterNumber, requiredNumber /*, iteration*/);
            break;
        default:
            break;
    }
}

void Node::sendStageReply(const uint8_t sender, const cs::Signature& signature, const MsgTypes msgType, const uint8_t requester, cs::Bytes& message) {
    csdebug() << __func__;
    csmeta(csdetails) << "started";

    if (myLevel_ != Level::Confidant) {
        cswarning() << "NODE> Only confidant nodes can send consensus stages";
        return;
    }

    const cs::Conveyer& conveyer = cs::Conveyer::instance();

    if (!conveyer.isConfidantExists(requester) || !conveyer.isConfidantExists(sender)) {
        return;
    }
    sendDefault(conveyer.confidantByIndex(requester), msgType, cs::Conveyer::instance().currentRoundNumber(), subRound_, signature, message);

    csmeta(csdetails) << "done";
}

void Node::sendSmartReject(const std::vector<RefExecution>& rejectList) {
    if (rejectList.empty()) {
        csmeta(cserror) << "must not send empty rejected contracts pack";
        return;
    }

    cs::Bytes data;
    cs::DataStream stream(data);

    stream << rejectList;

    csdebug() << "Node: sending " << rejectList.size() << " rejected contract(s) to related smart confidants";
    sendBroadcast(MsgTypes::RejectedContracts, cs::Conveyer::instance().currentRoundNumber(), data);
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

    sendToList(smartConfidants, stageOneInfo.sender, MsgTypes::FirstSmartStage, cs::Conveyer::instance().currentRoundNumber(),
               // payload
               stageOneInfo.message, stageOneInfo.signature);
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
    // hash of part received message
    stage.messageHash = cscrypto::calculateHash(stage.message.data(), stage.message.size());
    if (!cscrypto::verifySignature(stage.signature, sender, stage.messageHash.data(), stage.messageHash.size())) {
        cswarning() << "NODE> Smart stage One from " << cs::Utils::byteStreamToHex(sender.data(), sender.size()) << " -  WRONG SIGNATURE!!!";//
        return;
    }
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

    csdebug() << "NODE> SmartStage One from T[" << static_cast<int>(stage.sender) << "] is OK!";

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
    cs::Bytes bytes;
    istream_ >> bytes >> stage.signature;

    if (!istream_.good() || !istream_.end()) {
        cserror() << "NODE> Bad SmartStageTwo packet format";
        return;
    }

    cs::DataStream stream(bytes.data(), bytes.size());
    stream >> stage.sender;
    stream >> stage.id;
    stream >> stage.signatures;
    stream >> stage.hashes;

    csdebug() << "NODE> Read all data from the stream";

    if (!cscrypto::verifySignature(stage.signature, sender, bytes.data(), bytes.size())) {
        csdebug() << "NODE> Smart Stage Two from T[" << static_cast<int>(stage.sender) << "] -  WRONG SIGNATURE!!!";
        return;
    }

    csdebug() << "Signature is OK";
    stage.message = std::move(bytes);

    csmeta(csdetails) << "Signature is OK";

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
    cs::Bytes bytes;
    istream_ >> bytes >> stage.signature;

    if (!istream_.good() || !istream_.end()) {
        cserror() << "NODE> Bad SmartStage Three packet format";
        return;
    }

    cs::DataStream stream(bytes.data(), bytes.size());
    stream >> stage.sender;
    stream >> stage.writer;
    stream >> stage.id;
    stream >> stage.realTrustedMask;
    stream >> stage.packageSignature;

    if (!cscrypto::verifySignature(stage.signature, sender, bytes.data(), bytes.size())) {
        csdebug() << "SmartStage Three from T[" << static_cast<int>(stage.sender) << "] -  WRONG SIGNATURE!!!";
        return;
    }

    stage.message = std::move(bytes);

    csdebug() << "NODE> SmartStage-3 from T[" << static_cast<int>(stage.sender) << "] is OK!";

    emit gotSmartStageThree(stage, false);
}

void Node::smartStageRequest(MsgTypes msgType, uint64_t smartID, cs::PublicKey confidant, uint8_t respondent, uint8_t required) {
    sendDefault(confidant, msgType, cs::Conveyer::instance().currentRoundNumber(), smartID, respondent, required);
    csmeta(csdetails) << "done";
}

void Node::getSmartStageRequest(const MsgTypes msgType, const uint8_t* data, const size_t size, const cs::PublicKey& requester) {
    csmeta(csdetails) << "started";

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

    cs::PublicKey req = requester;

    emit receivedSmartStageRequest(msgType, smartID, requesterNumber, requiredNumber, req);
}

void Node::sendSmartStageReply(const cs::Bytes& message, const cs::Signature& signature, const MsgTypes msgType, const cs::PublicKey& requester) {
    csmeta(csdetails) << "started";

    sendDefault(requester, msgType, cs::Conveyer::instance().currentRoundNumber(), message, signature);
    csmeta(csdetails) << "done";
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
void Node::sendRoundPackage(const cs::RoundNumber rNum, const cs::PublicKey& target) {
    csdebug() << "Send round table: ";
    if (roundPackageCache_.size() == 0) {
        csdebug() << "No active round table, can't send";
        return;
    }
    auto rpCurrent = std::find_if(roundPackageCache_.begin(), roundPackageCache_.end(), [rNum](cs::RoundPackage& rp) {return rp.roundTable().round == rNum;});

    //////////////////////////////////////////////////
    //if (std::find(lastRoundPackage->roundTable().confidants.cbegin(), lastRoundPackage->roundTable().confidants.cend(), target) != lastRoundPackage->roundTable().confidants.end()) {
    //    solver_->changeHashCollectionTimer();
    //}
    sendDefault(target, MsgTypes::RoundTable, rpCurrent->roundTable().round, rpCurrent->subRound(), rpCurrent->toBinary());
    csdebug() << "Done";
    if (!rpCurrent->poolMetaInfo().characteristic.mask.empty()) {
        csmeta(csdebug) << "Packing " << rpCurrent->poolMetaInfo().characteristic.mask.size() << " bytes of char. mask to send";
    }
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
    performRoundPackage(rPackage, solver_->getPublicKey());
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
        uint64_t rpTimeStamp;
        try {
            lastTimeStamp = std::stoll(getBlockChain().getLastTimeStamp());
        }
        catch (...) {
            csdebug() << __func__ << ": last block Timestamp was announced as zero";
            return false;
        }

        try {
            currentTimeStamp = std::stoll(cs::Utils::currentTimestamp());
        }
        catch (...) {
            csdebug() << __func__ << ": current Timestamp was announced as zero";
            return false;
        }

        try {
            rpTimeStamp = std::stoll(rPackage.poolMetaInfo().timestamp);
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

            const auto ave_duration = stat_.getAveTime();
            if (speed < ave_duration / 10 && rPackage.roundTable().round - stat_.getNodeStartRound() > Consensus::SpeedCheckRound) {
                stat_.onRoundStart(rPackage.roundTable().round, true /*skip_logs*/);
                cserror() << "drop RoundPackage created in " << speed << " ms/block, average ms/round is " << ave_duration;
                return false;
            }
        }
    }
    return true;
}

void Node::getRoundTable(const uint8_t* data, const size_t size, const cs::RoundNumber rNum, const cs::PublicKey& sender) {
    csdebug() << "NODE> next round table received, round: " << rNum;
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

    if (!rpSpeedOk(rPackage)) {
        return;
    }

    


    csdebug() << "---------------------------------- RoundPackage #" << rPackage.roundTable().round << " --------------------------------------------- \n" 
        <<  rPackage.toString() 
        <<  "\n-----------------------------------------------------------------------------------------------------------------------------";


    cs::RoundNumber storedRound = conveyer.currentRoundNumber();
    conveyer.setRound(rNum);
    poolSynchronizer_->sync(conveyer.currentRoundNumber());
    if (poolSynchronizer_->isSyncroStarted()) {
        getCharacteristic(rPackage);
    }
    roundPackageCache_.push_back(rPackage);
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
    performRoundPackage(rPackage, sender);
}

void Node::performRoundPackage(cs::RoundPackage& rPackage, const cs::PublicKey& /*sender*/) {
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
    maxNeighboursSequence_ = rPackage.roundTable().round;

    auto it = recdBangs.begin();
    while (it != recdBangs.end()) {
        if (it->first < rPackage.roundTable().round) {
            it = recdBangs.erase(it);
            continue;
        }
        ++it;
    }

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

    onRoundStart(cs::Conveyer::instance().currentRoundTable());
	csinfo() << "Confidants: " << rPackage.roundTable().confidants.size() << ", Hashes: " << rPackage.roundTable().hashes.size();
    reviewConveyerHashes();

    csmeta(csdetails) << "done\n";
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
    cs::Bytes message;
    cs::DataStream stream(message);
    cs::Byte myTrustedSize = 0;
    cs::Byte myRealTrustedSize = 0;

    uint64_t lastTimeStamp = std::atoll(getBlockChain().getLastTimeStamp().c_str());
    uint64_t currentTimeStamp = std::atoll(cs::Utils::currentTimestamp().c_str());
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
    sendToConfidants(MsgTypes::BlockHash, round, subRound_, messageToSend, signature);
    csdebug() << "NODE> Hash sent, round: " << round << "." << cs::numeric_cast<int>(subRound_) << ", message: " << cs::Utils::byteStreamToHex(messageToSend);

}

void Node::getHash(const uint8_t* data, const size_t size, cs::RoundNumber rNum, const cs::PublicKey& sender) {
    if (myLevel_ != Level::Confidant) {
        csdebug() << "NODE> ignore hash as no confidant";
        return;
    }

    // TODO: here shoud be placed the DPOS check
    csdetails() << "NODE> get hash of round " << rNum << ", data size " << size;

    istream_.init(data, size);
    uint8_t subRound = 0;
    istream_ >> subRound;

    if (subRound > subRound_) {
        cswarning() << "NODE> We got hash for the Node with SUBROUND: " << static_cast<int>(subRound) << ", we don't have";
        // TODO : Maybe return
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
    if (!stream.size() == 0 || !stream.isValid()) {
        csdebug() << "Stream is a bit uncertain ... ";
    }
    uint64_t lastTimeStamp = std::atoll(getBlockChain().getLastTimeStamp().c_str());
    uint64_t currentTimeStamp = std::atoll(cs::Utils::currentTimestamp().c_str());

    csdebug() << "Got Hash message (" << tmp.size() << "): " << cs::Utils::byteStreamToHex(tmp.data(), tmp.size())
        << " : " << static_cast<int>(sHash.trustedSize) << " - " << static_cast<int>(sHash.realTrustedSize);
    csdebug() << "TimeStamp     = " << std::to_string(sHash.timeStamp);
    uint64_t deltaStamp = currentTimeStamp - lastTimeStamp;
    if (deltaStamp > Consensus::DefaultTimeStampRange) {
        deltaStamp = Consensus::DefaultTimeStampRange;

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
    //cs::PublicKeys confidants = cs::Conveyer::instance().confidants();

    if (roundPackageCache_.size() > 0) { // if cache.size > 0 current won't be  nullptr
        csdebug() << "roundPackageList.size() = " << roundPackageCache_.size();
        auto myTrustedSize = roundPackageCache_.back().poolMetaInfo().realTrustedMask.size();
        auto myRealTrustedSize = cs::TrustedMask::trustedSize(roundPackageCache_.back().poolMetaInfo().realTrustedMask);
        if (myRealTrustedSize < myTrustedSize && myRealTrustedSize < sHash.realTrustedSize) {
            csdebug() << "Starting RoundPackage request";
         //   roundPackageRequest(rNum, sender);
        }
    }

    solver_->gotHash(std::move(sHash));
}

void Node::roundPackRequest(const cs::PublicKey& respondent, cs::RoundNumber round) {
    csdebug() << "NODE> send request for round info  #" << round;
    sendDefault(respondent, MsgTypes::RoundPackRequest, round, round /*dummy data to prevent packet drop on receiver side*/);
}

void Node::getRoundPackRequest(const uint8_t* data, const size_t size, cs::RoundNumber rNum, const cs::PublicKey& sender) {
    csunused(data);
    csunused(size);

    csdebug() << "NODE> getting roundPack request #" << rNum;

    if (roundPackageCache_.size() == 0) {
        csdebug() << "NODE> can't send = don't have last RoundPackage filled";
        return;
    }
    cs::RoundPackage rp = roundPackageCache_.back();

    if (rp.roundTable().round == rNum) {
        if(!rp.roundSignatures().empty()) {
            roundPackReply(sender);
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
    sendDefault(respondent, MsgTypes::EmptyRoundPack, seq, signature);
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
    cs::Conveyer::instance().setRound(rNum);
    poolSynchronizer_->sync(cs::Conveyer::instance().currentRoundNumber());
}


void Node::roundPackReply(const cs::PublicKey& respondent) {
    csdebug() << "NODE> sending roundPack reply to " << cs::Utils::byteStreamToHex(respondent.data(), respondent.size());
    if (roundPackageCache_.size() == 0) {
        csdebug() << "NODE> can't send = don't have last RoundPackage filled";
        return;
    }
    cs::RoundPackage rp = roundPackageCache_.back();
    sendDefault(respondent, MsgTypes::RoundTable, rp.roundTable().round, rp.subRound(), rp.toBinary());
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
    sendDefault(respondent, MsgTypes::RoundTableRequest, round, myConfidantIndex_);
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

    sendDefault(target, MsgTypes::RoundTableReply, cs::Conveyer::instance().currentRoundNumber(), hasRequestedInfo);
}

bool Node::tryResendRoundTable(const cs::PublicKey& target, const cs::RoundNumber rNum) {
    if (lastSentRoundData_.table.round != rNum || lastSentRoundData_.subRound != subRound_) {
        csdebug() << "NODE> unable to repeat round data #" << rNum;
        return false;
    }

    csdebug() << "NODE> Re-send last round info #" << rNum << " to ALL";
    auto rPackage = std::find_if(roundPackageCache_.begin(), roundPackageCache_.end(), [rNum] (cs::RoundPackage& rp) {return rp.roundTable().round == rNum;});
    if (rPackage == roundPackageCache_.cend()) {
        return false;
    }
    sendRoundPackage(rNum, target);

    return true;
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

void Node::onRoundStart(const cs::RoundTable& roundTable) {
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
        if (stopRequested_) {
            stop();
            return;
        }
    }

    // TODO: think how to improve this code.
    stageOneMessage_.clear();
    stageOneMessage_.resize(roundTable.confidants.size());
    stageTwoMessage_.clear();
    stageTwoMessage_.resize(roundTable.confidants.size());
    stageThreeMessage_.clear();
    stageThreeMessage_.resize(roundTable.confidants.size());
    stageThreeSent_ = false;

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
    cslog() << " Last written sequence = " << WithDelimiters(blockChain_.getLastSeq()) << ", neighbours = " << transport_->getNeighboursCount();

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

    csdebug() << line2.str();
    stat_.onRoundStart(cs::Conveyer::instance().currentRoundNumber(), false /*skip_logs*/);
    csdebug() << line2.str();

    solver_->nextRound();

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
    sendDefault(respondent, MsgTypes::HashReply, cs::Conveyer::instance().currentRoundNumber(), subRound_, signature, getConfidantNumber(), hash);
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

    if (static_cast<size_t>(badHashReplySummary) > conveyer.confidantsCount() / 2) {
        csmeta(csdebug) << "This node really have not valid HASH!!! Removing last block from DB and trying to syncronize";
        // TODO: examine what will be done without this function
		blockChain_.setBlocksToBeRemoved(1U);
        blockChain_.removeLastBlock();
    }
}

/*static*/
void Node::requestStop() {
    emit stopRequested();
}

void Node::onStopRequested() {
    if (stopRequested_) {
        // subsequent request is handled as unconditional stop
        stop();
        return;
    }

    stopRequested_ = true;

    if (myLevel_ == Level::Confidant) {
        cslog() << "Node: wait until complete trusted role before exit";
    }
    else {
        stop();
    }
}

void Node::validateBlock(csdb::Pool block, bool* shouldStop) {
    if (stopRequested_) {
        *shouldStop = true;
        return;
    }
    if (!blockValidator_->validateBlock(block,
        cs::BlockValidator::ValidationLevel::hashIntergrity
            /*| cs::BlockValidator::ValidationLevel::smartStates*/
            /*| cs::BlockValidator::ValidationLevel::accountBalance */,
        cs::BlockValidator::SeverityLevel::onlyFatalErrors)) {
        *shouldStop = true;
        return;
    }
}
