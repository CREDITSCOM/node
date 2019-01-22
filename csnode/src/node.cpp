#include <algorithm>
#include <csignal>
#include <numeric>
#include <sstream>

#include <solver/solvercore.hpp>

#include <csnode/conveyer.hpp>
#include <csnode/datastream.hpp>
#include <csnode/node.hpp>
#include <csnode/nodecore.hpp>
#include <csnode/spammer.hpp>

#include <lib/system/logger.hpp>
#include <lib/system/utils.hpp>

#include <net/transport.hpp>

#include <base58.h>

#include <boost/optional.hpp>
#include <lib/system/progressbar.hpp>

#include <lz4.h>
#include <cscrypto/cscrypto.hpp>
#include <lib/system/signals.hpp>
#include <poolsynchronizer.hpp>

const unsigned MIN_CONFIDANTS = 3;
const unsigned MAX_CONFIDANTS = 100;

const csdb::Address Node::genesisAddress_ = csdb::Address::from_string("0000000000000000000000000000000000000000000000000000000000000001");
const csdb::Address Node::startAddress_ = csdb::Address::from_string("0000000000000000000000000000000000000000000000000000000000000002");

Node::Node(const Config& config)
: nodeIdKey_(config.getMyPublicKey())
, nodeIdPrivate_(config.getMyPrivateKey())
, blockChain_(genesisAddress_, startAddress_)
, solver_(new cs::SolverCore(this, genesisAddress_, startAddress_))
, allocator_(1 << 24, 5)
, packStreamAllocator_(1 << 26, 5)
, ostream_(&packStreamAllocator_, nodeIdKey_) {
  std::cout << "Start transport... ";
  transport_ = new Transport(config, this);
  std::cout << "Done\n";
  poolSynchronizer_ = new cs::PoolSynchronizer(config.getPoolSyncSettings(), transport_, &blockChain_);
  cs::Connector::connect(blockChain_.getStorage().read_block_event(), &stat_, &cs::RoundStat::onReadBlock);
  good_ = init(config);
}

Node::~Node() {
  sendingTimer_.stop();

  delete solver_;
  delete transport_;
  delete poolSynchronizer_;
}

bool Node::init(const Config& config) {
  if(!blockChain_.init(config.getPathToDB())) {
    return false;
  }
  cslog() << "Blockchain is init, contains " << stat_.totalAcceptedTransactions_ << " transactions";

#ifdef NODE_API
  std::cout << "Init API... ";
  papi_ = std::make_unique<csconnector::connector>(blockChain_, solver_,
    csconnector::Config {
     config.getApiSettings().port,
     config.getApiSettings().ajaxPort,
     config.getApiSettings().executorPort
    });
  std::cout << "Done\n";
#endif

  if (!transport_->isGood()) {
    return false;
  }
  std::cout << "Transport is init\n";

  if (!solver_) {
    return false;
  }
  std::cout << "Solver is init\n";

  std::cout << "Everything is init\n";

  solver_->setKeysPair(nodeIdKey_, nodeIdPrivate_);

#ifdef SPAMMER
  runSpammer();
  std::cout << "Spammer is init\n";
#endif

  cs::Connector::connect(&sendingTimer_.timeOut, this, &Node::processTimer);
  cs::Connector::connect(&cs::Conveyer::instance().flushSignal(), this, &Node::onTransactionsPacketFlushed);
  cs::Connector::connect(&poolSynchronizer_->sendRequest, this, &Node::sendBlockRequest);

  return true;
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

  auto bcStorage = blockChain_.getStorage();
  bcStorage.close();

  cswarning() << "[BLOCKCHAIN STORAGE CLOSED]";
}

void Node::runSpammer() {
  if (!spammer_) {
    cswarning() << "SolverCore: starting transaction spammer";
    spammer_ = std::make_unique<cs::Spammer>();
    spammer_->StartSpamming(*this);
  }
}

/* Requests */
void Node::flushCurrentTasks() {
  transport_->addTask(ostream_.getPackets(), ostream_.getPacketsCount());
  ostream_.clear();
}

void Node::getBigBang(const uint8_t* data, const size_t size, const cs::RoundNumber rNum) {
  cswarning() << "-----------------------------------------------------------";
  cswarning() << "NODE> BigBang #" << rNum
              << ": last written #" << blockChain_.getLastSequence()
              << ", current #" << roundNumber_;
  cswarning() << "-----------------------------------------------------------";

  istream_.init(data, size);
  istream_ >> subRound_;

  cs::Hash lastBlockHash;
  istream_ >> lastBlockHash;

  cs::RoundTable globalTable;
  globalTable.round = rNum;

  if (!readRoundData(globalTable)) {
    cserror() << className() << " read round data from SS failed";
    return;
  }

  // this evil code sould be removed after examination
  while (blockChain_.getLastSequence() >= rNum) {
    blockChain_.removeLastBlock();
  }

  // resend all this round data available
  csdebug() << "NODE> resend last block hash after BigBang";

  cs::Conveyer& conveyer = cs::Conveyer::instance();
  globalTable.hashes = conveyer.currentRoundTable().hashes;

  csmeta(csdebug) << "Get BigBang globalTable.hashes: " << globalTable.hashes.size();

  onRoundStart(globalTable);
  conveyer.updateRoundTable(std::move(globalTable));

  poolSynchronizer_->processingSync(globalTable.round, true);

  if (conveyer.isSyncCompleted()) {
    startConsensus();
  }
  else {
    sendPacketHashesRequest(conveyer.currentNeededHashes(), conveyer.currentRoundNumber(), startPacketRequestPoint_);
  }
}

void Node::getRoundTableSS(const uint8_t* data, const size_t size, const cs::RoundNumber rNum) {
  istream_.init(data, size);

  cslog() << "NODE> get SS Round Table #" << rNum;
  cs::RoundTable roundTable;

  if (!readRoundData(roundTable)) {
    cserror() << "NODE> read round data from SS failed, continue without round table";
  }
  subRound_ = 0;
  roundTable.round = rNum;

  // "normal" start
  if (roundTable.round == 1) {
    cs::Timer::singleShot(TIME_TO_AWAIT_SS_ROUND, cs::RunPolicy::CallQueuePolicy, [this, roundTable]() mutable {
      onRoundStart(roundTable);
      cs::Conveyer::instance().setRound(std::move(roundTable));
      reviewConveyerHashes();
    });

    return;
  }

  // "hot" start
  handleRoundMismatch(roundTable);
}

// handle mismatch between own round & global round, calling code should detect mismatch before calling to the method
void Node::handleRoundMismatch(const cs::RoundTable& globalTable) {
  const auto& localTable = cs::Conveyer::instance().currentRoundTable();

  if (localTable.round == globalTable.round) {
    // mismatch not confirmed
    return;
  }

  // global round is behind local one
  if (localTable.round > globalTable.round) {
    // TODO: in case of bigbang, rollback round(s), then accept global_table, then start round again

    if (localTable.round - globalTable.round == 1) {
      csdebug() << "NODE> we are a one round forward, wait";
      //csdebug() << "NODE> re-send last round info may help others to go to round #" << localTable.round;
      //tryResendRoundTable(std::nullopt, localTable.round);  // broadcast round info
    }
    else {
      // TODO: Test if we are in proper blockchain

      // TODO: rollback local round to global one

      cserror() << "NODE> round rollback (from #" << localTable.round << " to #" << globalTable.round
                << " not implemented yet";
    }
    return;
  }

  // local round is behind global one
  const auto last_block = blockChain_.getLastSequence();
  if (last_block + cs::Conveyer::HashTablesStorageCapacity < globalTable.round) {
    // activate pool synchronizer
    poolSynchronizer_->processingSync(globalTable.round);
    // no return, ask for next round info
  }
}

cs::RoundNumber Node::getRoundNumber() const {
  return roundNumber_;
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

void Node::getNodeStopRequest(const uint8_t* data, const std::size_t size) {
  istream_.init(data, size);

  uint16_t version = 0;
  istream_ >> version;

  if (!istream_.good()) {
    cswarning() << "NODE> Get stop request parsing failed";
    return;
  }

  cswarning() << "NODE> Get stop request, received version " << version << ", received bytes " << size;

  if (NODE_VERSION >= version) {
    cswarning() << "NODE> Get stop request, node version is okay, continue working";
    return;
  }

  cswarning() << "NODE> Get stop request, node will be closed...";

  cs::Timer::singleShot(TIME_TO_AWAIT_ACTIVITY << 5, cs::RunPolicy::CallQueuePolicy, [this] {
    stop();
  });
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

  csdebug() << "NODE> Get packet hashes reply: sender " << cs::Utils::byteStreamToHex(sender.data(), sender.size());
  csdebug() << "NODE> Hashes reply got packets count: " << packets.size();

  processPacketsReply(std::move(packets), round);
}

void Node::getCharacteristic(const uint8_t* data, const size_t size, const cs::RoundNumber round,
                             const cs::PublicKey& sender, std::vector<cs::SignaturePair>&& poolSignatures) {
  csmeta(csdetails) << "started";
  cs::Conveyer& conveyer = cs::Conveyer::instance();

  if (!conveyer.isSyncCompleted(round)) {
    csdebug() << "NODE> Packet sync not finished, saving characteristic meta to call after sync";

    cs::Bytes characteristicBytes(data, data + size);

    cs::CharacteristicMeta meta;
    meta.bytes = std::move(characteristicBytes);
    meta.sender = sender;
    meta.signatures = std::move(poolSignatures);

    conveyer.addCharacteristicMeta(round, std::move(meta));
    return;
  }

  cs::DataStream poolStream(data, size);
  csdebug() << "NODE> Conveyer sync completed, parsing data size (" << size  << ") -> " << cs::Utils::byteStreamToHex(data,size);

  cs::Characteristic characteristic;
  cs::PoolMetaInfo poolMetaInfo;
  size_t smartSigCount;
  csdb::Pool::SmartSignature tmpSmartSignature;

  poolStream >> poolMetaInfo.timestamp;
  poolStream >> characteristic.mask;
  poolStream >> poolMetaInfo.sequenceNumber;
  poolStream >> poolMetaInfo.realTrustedMask;
  poolStream >> poolMetaInfo.previousHash;
  poolStream >> smartSigCount;

  csdebug() << "Trying to get confidants from round " << round;
  const auto table = conveyer.roundTable(round);

  if (table == nullptr) {
    cserror() << "NODE> cannot access proper round table to add trusted to pool #" << poolMetaInfo.sequenceNumber;
    return;
  }

  csdebug() << "Real TrustedMask size = " << poolMetaInfo.realTrustedMask.size();

  for (size_t i = 0; i < poolMetaInfo.realTrustedMask.size(); ++i) {
    if (!conveyer.isConfidantExists(i)) {
      cserror() << csname() << "Bad index";
      return;
    }

    const auto& key = conveyer.confidantByIndex(i);

    if (poolMetaInfo.realTrustedMask[i] == 0) {
      poolMetaInfo.writerKey = key;
      csdebug() << "WriterKey =" << cs::Utils::byteStreamToHex(poolMetaInfo.writerKey.data(), poolMetaInfo.writerKey.size());
    }
    else {
      csdebug() << "PublicKey =" << cs::Utils::byteStreamToHex(key.data(), key.size());
    }
  }

  if (!istream_.good()) {
    csmeta(cserror) << "Round info parsing failed, data is corrupted";
    return;
  }

  csdebug() << "NODE> Sequence " << poolMetaInfo.sequenceNumber << ", mask size " << characteristic.mask.size();
  csdebug() << "NODE> Time: " << poolMetaInfo.timestamp;

  if (blockChain_.getLastSequence() <= poolMetaInfo.sequenceNumber) {
    // otherwise senseless, this block is already in chain
    stat_.totalReceivedTransactions_ += characteristic.mask.size();
    conveyer.setCharacteristic(characteristic, poolMetaInfo.sequenceNumber);

    std::optional<csdb::Pool> pool = conveyer.applyCharacteristic(poolMetaInfo);

    if (!pool.has_value()) {
      csmeta(cserror) << "Created pool is not valid";
      return;
    }

    pool.value().set_confidants(table->confidants);

    if (!blockChain_.storeBlock(pool.value(), false /*by_sync*/)) {
      cserror() << "NODE> failed to store block in BlockChain";
    }
    else {
      stat_.totalAcceptedTransactions_ += pool.value().transactions_count();
      blockChain_.testCachedBlocks();
    }

    size_t truePoolSignatures = 0;
    csdb::PoolHash tmpHash = blockChain_.getLastHash();

    const auto& hashBinary = tmpHash.to_binary();
    csdebug() << "Hash: " << cs::Utils::byteStreamToHex(hashBinary.data(), hashBinary.size());

    for (auto& it : poolSignatures) {
      if (!conveyer.isConfidantExists(it.sender)) {
        return;
      }

      const auto& confidant = conveyer.confidantByIndex(it.sender);

      if (cscrypto::VerifySignature(it.signature, confidant, hashBinary.data(), hashBinary.size())) {
        csdebug() << "Signature ["<< static_cast<int>(it.sender) << "] of " << cs::Utils::byteStreamToHex(confidant.data(), confidant.size()) << " is valid";
        ++truePoolSignatures;
        pool.value().add_signature(it.sender, it.signature);
      }
      else {
        csdebug() << "Signature [" << static_cast<int>(it.sender) << "] of " << cs::Utils::byteStreamToHex(confidant.data(), confidant.size()) << " is NOT valid";
      }
    }

    if (truePoolSignatures == poolSignatures.size()) {
      cswarning() << "All Pool Signatures Are Ok!";
    }
    else {
      cswarning() << "Some of Pool Signatures aren't valid. The pool will not be written to DB";
      //TODO: add code of eracing the just written block from blockchain
    }
  }

  csmeta(csdetails) << "done";
}

void Node::retriveSmartConfidants(const cs::Sequence startSmartRoundNumber, cs::ConfidantsKeys& confs) const {
  csmeta(csdebug);
  //возможна ошибка если на пишущем узле происходит запись блока в конце предыдущего раунда, а в других нодах в начале
  const cs::RoundTable* table = cs::Conveyer::instance().roundTable(startSmartRoundNumber);
  if (table == nullptr) {
    csdb::Pool tmpPool = blockChain_.loadBlock(startSmartRoundNumber);
    const auto& confidants = tmpPool.confidants();
    csdebug() << "___[" << startSmartRoundNumber << "] = [" << tmpPool.sequence() << "]: " << confidants.size();
    for (auto& key : confidants) {
      confs.emplace_back(key);
    }
    return;
  }

  confs = table->confidants;
}

void Node::sendTransactionsPacket(const cs::TransactionsPacket& packet) {
  if (packet.hash().isEmpty()) {
    cswarning() << "Send transaction packet with empty hash failed";
    return;
  }
  sendBroadcast(MsgTypes::TransactionPacket, roundNumber_, packet);
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
  main = (roundTable != nullptr) ? roundTable->general : conveyer.currentRoundTable().general;

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

  csdebug() << "NODE> Get block request> Getting the request for block: from: " << sequences.front()
          << ", to: " << sequences.back()
          << ", id: " << packetNum;

  if (sequences.front() > blockChain_.getLastSequence()) {
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

  std::size_t realBinSize = 0;
  RegionPtr memPtr = compressPoolsBlock(poolsBlock, realBinSize);

  tryToSendDirect(target, MsgTypes::RequestedBlock, cs::Conveyer::instance().currentRoundNumber(), realBinSize,
                  cs::numeric_cast<uint32_t>(memPtr.size()), memPtr, packetNum);
}

void Node::becomeWriter() {
  myLevel_ = NodeLevel::Writer;
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

    if (auto meta = conveyer.characteristicMeta(round); meta.has_value()) {
      csdebug() << "NODE> Run characteristic meta";
      getCharacteristic(meta->bytes.data(), meta->bytes.size(), round, meta->sender, std::move(meta->signatures));
    }

    // if next block maybe stored, the last written sequence maybe updated, so deferred consensus maybe resumed
    if (blockChain_.getLastSequence() + 1 == roundNumber_) {
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
  const bool isHashesEmpty =  conveyer.currentRoundTable().hashes.empty();

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

void Node::processTimer() {
  cs::Conveyer& conveyer = cs::Conveyer::instance();
  const auto round = conveyer.currentRoundNumber();

  if (myLevel_ == NodeLevel::Writer || round <= cs::TransactionsFlushRound) {
    return;
  }

  conveyer.flushTransactions();
}

void Node::onTransactionsPacketFlushed(const cs::TransactionsPacket& packet) {
  CallsQueue::instance().insert(std::bind(&Node::sendTransactionsPacket, this, packet));
}

void Node::sendBlockRequest(const ConnectionPtr target, const cs::PoolsRequestedSequences& sequences, std::size_t packetNum) {
  const auto round = cs::Conveyer::instance().currentRoundNumber();
  csmeta(csdetails) << "Target out(): " << target->getOut()
                    << ", sequence from: " << sequences.front()
                    << ", to: " << sequences.back()
                    << ", packet: " << packetNum
                    << ", round: " << round;

  ostream_.init(BaseFlags::Neighbours | BaseFlags::Signed | BaseFlags::Compressed);
  ostream_ << MsgTypes::BlockRequest;
  ostream_ << round;
  ostream_ << sequences;
  ostream_ << packetNum;

  transport_->deliverDirect(ostream_.getPackets(), ostream_.getPacketsCount(), target);

  ostream_.clear();
}

Node::MessageActions Node::chooseMessageAction(const cs::RoundNumber rNum, const MsgTypes type) {
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

  if (type == MsgTypes::FirstSmartStage || type == MsgTypes::SecondSmartStage || type == MsgTypes::ThirdSmartStage) {
    return MessageActions::Process;
  }

  if (type == MsgTypes::NodeStopRequest) {
    return MessageActions::Process;
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

  // outrunning packets of other types talk about round lag
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
        cs::RoundTable emptyRoundTable;
        emptyRoundTable.round = rNum;
        handleRoundMismatch(emptyRoundTable);
      }

      return MessageActions::Drop;
    }
  }

  if (type == MsgTypes::NewCharacteristic) {
    return MessageActions::Process;
  }

  if (type == MsgTypes::TransactionPacket) {
    return MessageActions::Process;
  }

  if (type == MsgTypes::TransactionsPacketRequest) {
    return MessageActions::Process;
  }

  if (type == TransactionsPacketReply) {
    return MessageActions::Process;
  }

  if (type == MsgTypes::RoundTableRequest) {
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

    if (rNum > blockChain_.getLastSequence() + cs::Conveyer::HashTablesStorageCapacity) {
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

  return (rNum == round ? MessageActions::Process : MessageActions::Postpone);
}

inline bool Node::readRoundData(cs::RoundTable& roundTable) {
  cs::PublicKey mainNode;

  uint8_t confSize = 0;
  istream_ >> confSize;

  csdebug() << "NODE> Number of confidants :" << cs::numeric_cast<int>(confSize);

  if (confSize < MIN_CONFIDANTS || confSize > MAX_CONFIDANTS) {
    cswarning() << "Bad confidants num";
    return false;
  }

  cs::ConfidantsKeys confidants;
  confidants.reserve(confSize);

  istream_ >> mainNode;

  // TODO Fix confidants array getting (From SS)
  while (istream_) {
    cs::PublicKey key;
    istream_ >> key;

    confidants.push_back(std::move(key));

    if (confidants.size() == confSize && !istream_.end()) {
      cswarning() << "Too many confidant nodes received";
      return false;
    }
  }

  if (!istream_.good() || confidants.size() < confSize) {
    cswarning() << "Bad round table format, ignoring";
    return false;
  }

  roundTable.confidants = std::move(confidants);
  roundTable.general = mainNode;
  roundTable.hashes.clear();

  return true;
}

static const char* nodeLevelToString(NodeLevel nodeLevel) {
  switch (nodeLevel) {
    case NodeLevel::Normal:
      return "Normal";
    case NodeLevel::Confidant:
      return "Confidant";
    case NodeLevel::Main:
      return "Main";
    case NodeLevel::Writer:
      return "Writer";
  }

  return "UNKNOWN";
}

std::ostream& operator<<(std::ostream& os, NodeLevel nodeLevel) {
  os << nodeLevelToString(nodeLevel);
  return os;
}

template<typename... Args>
void Node::sendDefault(const cs::PublicKey& target, const MsgTypes msgType, const cs::RoundNumber round, Args&&... args) {
  static constexpr cs::Byte defautFlags = BaseFlags::Fragmented;

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
  ostream_.init(BaseFlags::Neighbours | BaseFlags::Broadcast | BaseFlags::Fragmented | BaseFlags::Compressed);
  ostream_ << msgType << round;

  writeDefaultStream(std::forward<Args>(args)...);

  csdetails() << "NODE> Sending Direct data: packets count: " << ostream_.getPacketsCount()
              << ", last packet size: " << ostream_.getCurrentSize()
              << ", out: " << target->out
              << ", in: " << target->in
              << ", specialOut: " << target->specialOut
              << ", msgType: " << getMsgTypesString(msgType);

  transport_->deliverDirect(ostream_.getPackets(), ostream_.getPacketsCount(), target);
  ostream_.clear();
}

template <class... Args>
void Node::sendBroadcast(const MsgTypes msgType, const cs::RoundNumber round, Args&&... args) {
  ostream_.init(BaseFlags::Broadcast | BaseFlags::Fragmented | BaseFlags::Compressed);
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
  ConnectionPtr target = transport_->getRandomNeighbour();

  if (target) {
    sendToNeighbour(target, msgType, round, std::forward<Args>(args)...);
  }

  return target;
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
  (ostream_ << ... << std::forward<Args>(args));  // fold expression
}

template<typename... Args>
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

  csdetails() << "NODE> Sending broadcast data: size: " << ostream_.getCurrentSize()
              << ", last packet size: " << ostream_.getCurrentSize()
              << ", round: " << round
              << ", msgType: " << getMsgTypesString(msgType);

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

  const int compressedSize = LZ4_compress_default(data,
                                                  static_cast<char*>(memPtr.get()),
                                                  binSize,
                                                  cs::numeric_cast<int>(memPtr.size()));

  if (!compressedSize) {
    csmeta(cserror) << "Compress poools block error";
  }

  allocator_.shrinkLast(cs::numeric_cast<uint32_t>(compressedSize));

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

  const int uncompressedSize = LZ4_decompress_safe(static_cast<char*>(memPtr.get()),
                                                   bytesData,
                                                   cs::numeric_cast<int>(compressSize),
                                                   cs::numeric_cast<int>(realBinSize));

  if (uncompressedSize < 0) {
    csmeta(cserror) << "Decompress poools block error";
  }

  cs::DataStream stream(bytes.data(), bytes.size());
  cs::PoolsBlock poolsBlock;

  stream >> poolsBlock;

  return poolsBlock;
}

void Node::sendStageOne(cs::StageOne& stageOneInfo) {
  corruptionLevel = 0;
  if (myLevel_ != NodeLevel::Confidant) {
    cswarning() << "NODE> Only confidant nodes can send consensus stages";
    return;
  }

  stageOneInfo.roundTimeStamp = cs::Utils::currentTimestamp();

  csmeta(csdebug) << "Round: " << roundNumber_ << "." << cs::numeric_cast<int>(subRound_)
                << ", Sender: " << static_cast<int>(stageOneInfo.sender)
                << ", Cand Amount: " << stageOneInfo.trustedCandidates.size()
                << ", Hashes Amount: " << stageOneInfo.hashesCandidates.size()
                << ", Time Stamp: " << stageOneInfo.roundTimeStamp;
  csmeta(csdetails) << "Hash: " << cs::Utils::byteStreamToHex(stageOneInfo.hash.data(), stageOneInfo.hash.size());

  size_t expectedMessageSize = sizeof(stageOneInfo.sender)
                             + sizeof(stageOneInfo.hash)
                             + sizeof(stageOneInfo.trustedCandidates.size())
                             + sizeof(cs::PublicKey) * stageOneInfo.trustedCandidates.size()
                             + sizeof(stageOneInfo.hashesCandidates.size())
                             + sizeof(cs::Hash) * stageOneInfo.hashesCandidates.size()
                             + sizeof(stageOneInfo.roundTimeStamp.size())
                             + stageOneInfo.roundTimeStamp.size();

  cs::Bytes message;
  message.reserve(expectedMessageSize);

  cs::Bytes messageToSign;
  messageToSign.reserve(sizeof(cs::RoundNumber) + sizeof(uint8_t) + sizeof(cs::Hash));

  cs::DataStream stream(message);
  stream << stageOneInfo.sender;
  stream << stageOneInfo.hash;
  stream << stageOneInfo.trustedCandidates;
  stream << stageOneInfo.hashesCandidates;
  stream << stageOneInfo.roundTimeStamp;

  // hash of message
  cscrypto::CalculateHash(stageOneInfo.messageHash, message.data(), message.size());

  cs::DataStream signStream(messageToSign);
  signStream << roundNumber_;
  signStream << subRound_;
  signStream << stageOneInfo.messageHash;

  // signature of round number + calculated hash
  cscrypto::GenerateSignature(stageOneInfo.signature, solver_->getPrivateKey(), messageToSign.data(), messageToSign.size());

  const int k1 = (corruptionLevel / 1) % 2;
  const cs::Byte k2 = static_cast<cs::Byte>(corruptionLevel / 16);
  if (k1 == 1 && k2 == myConfidantIndex_) {
    csdebug() << "STAGE ONE ##############> NOTHING WILL BE SENT";
  }
  else {
    sendToConfidants(MsgTypes::FirstStage, roundNumber_, subRound_, stageOneInfo.signature, message);
  }

  csmeta(csdetails) << "Sent message size " << message.size();

  // cache
  stageOneMessage_[myConfidantIndex_] = std::move(message);
  csmeta(csdetails) << "done";
}

void Node::getStageOne(const uint8_t* data, const size_t size, const cs::PublicKey& sender) {
  csmeta(csdetails) << "started";

  if (myLevel_ != NodeLevel::Confidant) {
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

  cs::Bytes bytes;
  istream_ >> bytes;

  if (!istream_.good() || !istream_.end()) {
    csmeta(cserror) << "Bad stage-1 packet format";
    return;
  }

  // hash of part received message
  cscrypto::CalculateHash(stage.messageHash, bytes.data(), bytes.size());

  cs::Bytes signedMessage;
  cs::DataStream signedStream(signedMessage);
  signedStream << roundNumber_;
  signedStream << subRound_;
  signedStream << stage.messageHash;

  // stream for main message
  cs::DataStream stream(bytes.data(), bytes.size());
  stream >> stage.sender;
  stream >> stage.hash;
  stream >> stage.trustedCandidates;
  stream >> stage.hashesCandidates;
  stream >> stage.roundTimeStamp;

  const cs::Conveyer& conveyer = cs::Conveyer::instance();

  if (!conveyer.isConfidantExists(stage.sender)) {
    return;
  }

  const cs::PublicKey& confidant = conveyer.confidantByIndex(stage.sender);

  if (!cscrypto::VerifySignature(stage.signature, confidant, signedMessage.data(), signedMessage.size())) {
    cswarning() << "NODE> Stage-1 from T[" << static_cast<int>(stage.sender) << "] -  WRONG SIGNATURE!!!";
    return;
  }

  stageOneMessage_[stage.sender] = std::move(bytes);

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

  if (myLevel_ != NodeLevel::Confidant && myLevel_ != NodeLevel::Writer) {
    cswarning() << "Only confidant nodes can send consensus stages";
    return;
  }

  // TODO: fix it by logic changing
  const size_t confidantsCount = cs::Conveyer::instance().confidantsCount();
  const size_t stageBytesSize  = sizeof(stageTwoInfo.sender)
                               + sizeof(size_t) // count of signatures
                               + sizeof(size_t) // count of hashes
                               + (sizeof(cs::Signature) + sizeof(cs::Hash)) * confidantsCount; // signature + hash items

  cs::Bytes bytes;
  bytes.reserve(stageBytesSize);

  cs::DataStream stream(bytes);
  stream << stageTwoInfo.sender;
  stream << stageTwoInfo.signatures;
  stream << stageTwoInfo.hashes;

  // create signature
  cscrypto::GenerateSignature(stageTwoInfo.signature, solver_->getPrivateKey(), bytes.data(), bytes.size());

  const int k1 = (corruptionLevel / 2) % 2;
  const cs::Byte k2 = static_cast<cs::Byte>(corruptionLevel / 16);

  if (k1 == 1 && k2 == myConfidantIndex_) {
    csdebug() << "STAGE TWO ##############> NOTHING WILL BE SENT";
  }
  else {
    sendToConfidants(MsgTypes::SecondStage, roundNumber_, subRound_, stageTwoInfo.signature, bytes);
  }

  // cash our stage two
  csmeta(csdetails) << "Bytes size " << bytes.size();

  stageTwoMessage_[myConfidantIndex_] = std::move(bytes);

  csmeta(csdetails) << "done";
}

void Node::getStageTwo(const uint8_t* data, const size_t size, const cs::PublicKey& sender) {
  csmeta(csdetails);

  if (myLevel_ != NodeLevel::Confidant && myLevel_ != NodeLevel::Writer) {
    csdebug() << "NODE> ignore stage-2 as no confidant";
    return;
  }

  csdebug() << "NODE> getting stage-2 from " << getSenderText(sender);

  istream_.init(data, size);

  uint8_t subRound;
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
  
  if (!cscrypto::VerifySignature(stage.signature, conveyer.confidantByIndex(stage.sender), bytes.data(), bytes.size())) {
    csdebug() << "NODE> stage-2 [" << static_cast<int>(stage.sender) << "] -  WRONG SIGNATURE!!!";
    return;
  }

  csmeta(csdetails) << "Signature is OK";
  stageTwoMessage_[stage.sender] = std::move(bytes);

  csdebug() << "NODE> stage-2 [" << static_cast<int>(stage.sender) << "] is OK!";
  solver_->gotStageTwo(stage);
}

void Node::sendStageThree(cs::StageThree& stageThreeInfo) {
  csmeta(csdetails) << "started";

  if (myLevel_ != NodeLevel::Confidant) {
    cswarning() << "NODE> Only confidant nodes can send consensus stages";
    return;
  }

  // TODO: think how to improve this code
  const size_t stageSize = 2 * sizeof(uint8_t) + 2 * sizeof(cs::Signature) + stageThreeInfo.realTrustedMask.size();

  cs::Bytes bytes;
  bytes.reserve(stageSize);

  cs::DataStream stream(bytes);
  stream << stageThreeInfo.sender;
  stream << stageThreeInfo.writer;
  stream << stageThreeInfo.blockSignature;
  stream << stageThreeInfo.roundSignature;
  stream << stageThreeInfo.realTrustedMask;

  //cscrypto::GenerateSignature(stageThreeInfo.blockSignature, solver_->getPrivateKey(), stageThreeInfo.blockHash.data(), stageThreeInfo.blockHash.size());
  //cscrypto::GenerateSignature(stageThreeInfo.roundSignature, solver_->getPrivateKey(), stageThreeInfo.roundHash.data(), stageThreeInfo.roundHash.size());
  cscrypto::GenerateSignature(stageThreeInfo.signature,solver_->getPrivateKey(), bytes.data(), bytes.size());

  const int k1 = (corruptionLevel / 4) % 2;
  const cs::Byte k2 = static_cast<cs::Byte>(corruptionLevel / 16);

  if (k1 == 1 && k2 == myConfidantIndex_) {
    csdebug() << "STAGE THREE ##############> NOTHING WILL BE SENT";
  }
  else {
    sendToConfidants(MsgTypes::ThirdStage, roundNumber_, subRound_, stageThreeInfo.signature, bytes);
  }

  // cach stage three
  csmeta(csdetails) << "bytes size " << bytes.size();
  stageThreeMessage_[myConfidantIndex_] = std::move(bytes);
  stageThreeSent = true;
  csmeta(csdetails) << "done";
}

void Node::getStageThree(const uint8_t* data, const size_t size) {
  csmeta(csdetails);

  if (myLevel_ != NodeLevel::Confidant && myLevel_ != NodeLevel::Writer) {
    csdebug() << "NODE> ignore stage-3 as no confidant";
    return;
  }

  istream_.init(data, size);
  uint8_t subRound;
  istream_ >> subRound;

  if (subRound != subRound_) {
    cswarning() << "NODE> We got Stage-2 for the Node with SUBROUND, we don't have";
    return;
  }

  cs::StageThree stage;
  istream_ >> stage.signature;

  cs::Bytes bytes;
  istream_  >> bytes;

  if (!istream_.good() || !istream_.end()) {
    cserror() << "NODE> Bad stage-3 packet format";
    return;
  }

  cs::DataStream stream(bytes.data(), bytes.size());
  stream >> stage.sender;
  stream >> stage.writer;
  stream >> stage.blockSignature;
  stream >> stage.roundSignature;
  stream >> stage.realTrustedMask;

  const cs::Conveyer& conveyer = cs::Conveyer::instance();

  if (!conveyer.isConfidantExists(stage.sender)) {
    return;
  }

  if (!cscrypto::VerifySignature(stage.signature, conveyer.confidantByIndex(stage.sender), bytes.data(), bytes.size())) {
    cswarning() << "NODE> stage-3 from T[" << static_cast<int>(stage.sender) << "] -  WRONG SIGNATURE!!!";
    return;
  }
 
  stageThreeMessage_[stage.sender] = std::move(bytes);

  csdebug() << "NODE> stage-3 from T[" << static_cast<int>(stage.sender) << "] is OK!";

  solver_->gotStageThree(std::move(stage), (stageThreeSent ? 2 : 0));
}

void Node::stageRequest(MsgTypes msgType, uint8_t respondent, uint8_t required) {
  if (myLevel_ != NodeLevel::Confidant && myLevel_ != NodeLevel::Writer) {
    cswarning() << "NODE> Only confidant nodes can request consensus stages";
    return;
  }

  const cs::Conveyer& conveyer = cs::Conveyer::instance();

  if (!conveyer.isConfidantExists(respondent)) {
    return;
  }

  sendDefault(conveyer.confidantByIndex(respondent), msgType, roundNumber_ , subRound_,  myConfidantIndex_, required);
  csmeta(csdetails) << "done";
}

void Node::getStageRequest(const MsgTypes msgType, const uint8_t* data, const size_t size, const cs::PublicKey& requester) {
  csmeta(csdetails) << "started";

  if (myLevel_ != NodeLevel::Confidant) {
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

  if (!conveyer.isConfidantExists(requesterNumber) ||
      requester != conveyer.confidantByIndex(requesterNumber)) {
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
    solver_->gotStageThreeRequest(requesterNumber, requiredNumber);
    break;
  default:
    break;
  }
}

void Node::sendStageReply(const uint8_t sender, const cs::Signature& signature, const MsgTypes msgType, const uint8_t requester) {
  csmeta(csdetails) << "started";

  if (myLevel_ != NodeLevel::Confidant) {
    cswarning() << "NODE> Only confidant nodes can send consensus stages";
    return;
  }

  const cs::Conveyer& conveyer = cs::Conveyer::instance();

  if (!conveyer.isConfidantExists(requester) || !conveyer.isConfidantExists(sender)) {
    return;
  }

  cs::Bytes message;

  switch (msgType) {
  case MsgTypes::FirstStage:
    message = stageOneMessage_[sender];
    break;
  case MsgTypes::SecondStage:
    message = stageTwoMessage_[sender];
    break;
  case MsgTypes::ThirdStage:
    message = stageThreeMessage_[sender];
    break;
  default:
    break;
  }

  const int k1 = (corruptionLevel / 8) % 2;
  const cs::Byte k2 = static_cast<cs::Byte>(corruptionLevel / 16);

  if (k1 == 1 && k2 == myConfidantIndex_) {
    csdebug() << "STAGE REPLY ##############> NOTHING WILL BE SENT";
   }
  else {
    sendDefault(conveyer.confidantByIndex(requester), msgType, roundNumber_, subRound_, signature, message);
  }

  csmeta(csdetails) << "done";
}

void Node::sendSmartStageOne(cs::StageOneSmarts& stageOneInfo) {
  csmeta(csdebug) << "started";
  if (solver_->ownSmartsConfidantNumber() == cs::ConfidantConsts::InvalidConfidantIndex) {
    cswarning() << "NODE> Only confidant nodes can send smart-contract consensus stages";
    return;
  }

  csmeta(csdetails) << std::endl
                    << "Smart starting Round: " << stageOneInfo.sRoundNum << std::endl
                    << "Sender: " << static_cast<int>(stageOneInfo.sender) << std::endl
                    << "Hash: " << cs::Utils::byteStreamToHex(stageOneInfo.hash.data(), stageOneInfo.hash.size());

  size_t expectedMessageSize = sizeof(stageOneInfo.sender) + stageOneInfo.hash.size();

  cs::Bytes message;
  message.reserve(expectedMessageSize);

  cs::Bytes messageToSign;
  messageToSign.reserve(sizeof(cs::RoundNumber) + sizeof(cs::Hash));

  cs::DataStream stream(message);
  stream << stageOneInfo.sender;
  stream << stageOneInfo.hash;

  // hash of message
  cscrypto::CalculateHash(stageOneInfo.messageHash, message.data(), message.size());

  cs::DataStream signStream(messageToSign);
  signStream << solver_->smartRoundNumber();
  signStream << stageOneInfo.messageHash;

  csdebug() << "MsgHash: " << cs::Utils::byteStreamToHex(stageOneInfo.messageHash.data(), stageOneInfo.messageHash.size());

  // signature of round number + calculated hash
  cscrypto::GenerateSignature(stageOneInfo.signature, solver_->getPrivateKey(), messageToSign.data(), messageToSign.size());

  sendToList(solver_->smartConfidants(), solver_->ownSmartsConfidantNumber(), MsgTypes::FirstSmartStage, static_cast<cs::RoundNumber>(stageOneInfo.sRoundNum), stageOneInfo.signature, message);

  // cache
  smartStageOneMessage_[solver_->ownSmartsConfidantNumber()] = std::move(message);
  csmeta(csdebug) << "done";
}

void Node::smartStagesStorageClear(size_t cSize) {
  smartStageOneMessage_.clear();
  smartStageOneMessage_.resize(cSize);
  smartStageTwoMessage_.clear();
  smartStageTwoMessage_.resize(cSize);
  smartStageThreeMessage_.clear();
  smartStageThreeMessage_.resize(cSize);

  csmeta(csdetails) << " SmartStagesStorage prepared, martStageTemporary_.size() = " << smartStageTemporary_.size();
  for (size_t i = 0; i<smartStageTemporary_.size(); i++) {
    auto& it = smartStageTemporary_.at(i);
    if (it.msgRoundNum == solver_->smartRoundNumber()) {
      auto str = reinterpret_cast<cs::Byte*>(it.msgData.data());
      switch(it.msgType) {
        case (MsgTypes::FirstSmartStage):
          getSmartStageOne(str, it.msgData.size(),it.msgRoundNum, it.msgSender);
          break;
        case (MsgTypes::SecondSmartStage):
          getSmartStageTwo(str, it.msgData.size(), it.msgRoundNum, it.msgSender);
          break;
        case (MsgTypes::ThirdSmartStage):
          getSmartStageThree(str, it.msgData.size(), it.msgRoundNum, it.msgSender);
          break;
        default: break;
      }
    }
  }

  smartStageTemporary_.clear();
}

void Node::getSmartStageOne(const uint8_t* data, const size_t size, const cs::RoundNumber rNum, const cs::PublicKey& sender) {
  csmeta(csdetails) << "started";
  if (rNum != solver_->smartRoundNumber()) {
    cs::Stage st;
    st.msgType = MsgTypes::FirstSmartStage;
    //std::copy(data, data+size, st.msgData.data());
    st.msgData = std::string(reinterpret_cast<const char*>(data), size);
    //TODO: replace this parcing with the propriate one
    //stageStream >> st.msgData;
    st.msgRoundNum = rNum;
    st.msgSender = sender;

    csdebug() << "Get SmartStageOne Message(saving): " << cs::Utils::byteStreamToHex(data,size);
    csdebug() << "Get SmartStageOne Message(saved) : " << cs::Utils::byteStreamToHex(st.msgData.data(), st.msgData.size());
    csdebug() << "Stage stored in smartStageTemporary - won't be used until the correct SmartsRoundNumber comes";

    smartStageTemporary_.emplace_back(st);
  }

  if (solver_->ownSmartsConfidantNumber() == cs::ConfidantConsts::InvalidConfidantIndex) {
    csdebug() << "NODE> ignore smartStage-1 as no confidant";
    return;
  }
  csdebug() << "Get Smart Stage One Message(recover): " << cs::Utils::byteStreamToHex(data, size);

  istream_.init(data, size);

  cs::StageOneSmarts stage;
  stage.sRoundNum = rNum;
  istream_ >> stage.signature;

  cs::Bytes bytes;
  istream_ >> bytes;

  if (!istream_.good() || !istream_.end()) {
    cserror() << "Bad Smart Stage One packet format";
    return;
  }

  // hash of part received message
  cscrypto::CalculateHash(stage.messageHash, bytes.data(), bytes.size());
  csdebug() << "MsgHash: " << cs::Utils::byteStreamToHex(stage.messageHash.data(), stage.messageHash.size());
  cs::Bytes signedMessage;
  cs::DataStream signedStream(signedMessage);
  signedStream << stage.sRoundNum;
  signedStream << stage.messageHash;

  // stream for main message
  cs::DataStream stream(bytes.data(), bytes.size());
  stream >> stage.sender;
  stream >> stage.hash;

  const cs::ConfidantsKeys& smartConfidants = solver_->smartConfidants();

  if (stage.sender >= smartConfidants.size()) {
    cswarning() << "NODE> WRONG sender number";
    return;
  }

  const cs::PublicKey& confidant = smartConfidants.at(stage.sender);

  if (!cscrypto::VerifySignature(stage.signature, confidant, signedMessage.data(), signedMessage.size())) {
    cswarning() << "NODE> Stage One from T[" << static_cast<int>(stage.sender) << "] -  WRONG SIGNATURE!!!";
    return;
  }

  smartStageOneMessage_[stage.sender] = std::move(bytes);

  csmeta(csdebug) << "Sender: " << static_cast<int>(stage.sender)
                << ", sender key: " << cs::Utils::byteStreamToHex(confidant.data(), confidant.size())
                << " - " << cs::Utils::byteStreamToHex(sender.data(), sender.size());
  csdebug() << "Hash: " << cs::Utils::byteStreamToHex(stage.hash.data(), stage.hash.size());

  csdebug() << "NODE> Stage One from T[" << static_cast<int>(stage.sender) << "] is OK!";
  solver_->addSmartStageOne(stage, false);
}

void Node::sendSmartStageTwo(cs::StageTwoSmarts& stageTwoInfo) {
  csmeta(csdebug) << "started";

  if (solver_->ownSmartsConfidantNumber() == cs::ConfidantConsts::InvalidConfidantIndex) {
    cswarning() << "Only confidant nodes can send smart-contract consensus stages";
    return;
  }

  // TODO: fix it by logic changing

  size_t confidantsCount = cs::Conveyer::instance().confidantsCount();
  size_t stageBytesSize = sizeof(stageTwoInfo.sender) + (sizeof(cs::Signature) + sizeof(cs::Hash)) * confidantsCount;

  cs::Bytes bytes;
  bytes.reserve(stageBytesSize);

  cs::DataStream stream(bytes);
  stream << stageTwoInfo.sender;
  stream << stageTwoInfo.signatures;
  stream << stageTwoInfo.hashes;

  // create signature
  cscrypto::GenerateSignature(stageTwoInfo.signature, solver_->getPrivateKey(), bytes.data(), bytes.size());
  sendToList(solver_->smartConfidants(), solver_->ownSmartsConfidantNumber(), MsgTypes::SecondSmartStage, stageTwoInfo.sRoundNum, stageTwoInfo.signature, bytes);

  // cash our stage two
  smartStageTwoMessage_[solver_->ownSmartsConfidantNumber()] = std::move(bytes);
  csmeta(csdebug) << "done";
}

void Node::getSmartStageTwo(const uint8_t* data, const size_t size, const cs::RoundNumber rNum, const cs::PublicKey& sender) {
  csmeta(csdebug);

  if (solver_->ownSmartsConfidantNumber() == cs::ConfidantConsts::InvalidConfidantIndex) {
    csdebug() << "NODE> ignore SmartStage two as no confidant";
    return;
  }

  csdebug() << "NODE> Getting Stage Two from " << cs::Utils::byteStreamToHex(sender.data(), sender.size());

  istream_.init(data, size);

  cs::StageTwoSmarts stage;
  stage.sRoundNum = rNum;
  istream_ >> stage.signature;

  cs::Bytes bytes;
  istream_ >> bytes;

  if (!istream_.good() || !istream_.end()) {
    cserror() << "NODE> Bad StageTwo packet format";
    return;
  }

  cs::DataStream stream(bytes.data(), bytes.size());
  stream >> stage.sender;
  stream >> stage.signatures;
  stream >> stage.hashes;

  if (stage.sRoundNum != solver_->smartRoundNumber()) {
    cswarning() << "NODE> Bad Smart's RoundNumber";
    return;
  }

  csmeta(csdebug) << "Sender:" << cs::Utils::byteStreamToHex(sender.data(), sender.size());

  const cs::ConfidantsKeys& smartConfidants = solver_->smartConfidants();

  if (stage.sender >= smartConfidants.size()) {
    cswarning() << "NODE> WRONG sender number";
    return;
  }

  const cs::PublicKey& confidant = smartConfidants.at(stage.sender);

  if (!cscrypto::VerifySignature(stage.signature, confidant, bytes.data(), bytes.size())) {
    csdebug() << "NODE> Stage Two from T[" << static_cast<int>(stage.sender) << "] -  WRONG SIGNATURE!!!";
    return;
  }

  csmeta(csdetails) << "Signature is OK";
  smartStageTwoMessage_[stage.sender] = std::move(bytes);

  csdebug() << "NODE> Stage Two from T[" << static_cast<int>(stage.sender) << "] is OK!";
  solver_->addSmartStageTwo(stage, false);
}

void Node::sendSmartStageThree(cs::StageThreeSmarts& stageThreeInfo) {
  csmeta(csdebug) << "started";

  if (solver_->ownSmartsConfidantNumber() == cs::ConfidantConsts::InvalidConfidantIndex) {
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
  stream << stageThreeInfo.realTrustedMask;
  stream << stageThreeInfo.packageSignature;

  cscrypto::GenerateSignature(stageThreeInfo.signature, solver_->getPrivateKey(), bytes.data(), bytes.size());
  sendToList(solver_->smartConfidants(), solver_->ownSmartsConfidantNumber(), MsgTypes::ThirdSmartStage, stageThreeInfo.sRoundNum, stageThreeInfo.signature, bytes);
  
  // cach stage three
  smartStageThreeMessage_[solver_->ownSmartsConfidantNumber()] = std::move(bytes);
  csmeta(csdebug) << "done";
}

void Node::getSmartStageThree(const uint8_t* data, const size_t size, const cs::RoundNumber rNum, const cs::PublicKey& sender) {
  csmeta(csdetails) << "started";
  csunused(sender);

  if (solver_->ownSmartsConfidantNumber() == cs::ConfidantConsts::InvalidConfidantIndex) {
    csdebug() << "NODE> ignore SmartStage-3 as no confidant";
    return;
  }

  istream_.init(data, size);

  cs::StageThreeSmarts stage;
  stage.sRoundNum = rNum;
  istream_ >> stage.signature;

  cs::Bytes bytes;
  istream_ >> bytes;

  if (!istream_.good() || !istream_.end()) {
    cserror() << "NODE> Bad Smart Stage Three packet format";
    return;
  }

  cs::DataStream stream(bytes.data(), bytes.size());
  stream >> stage.sender;
  stream >> stage.writer;
  stream >> stage.realTrustedMask;
  stream >> stage.packageSignature;

  if (!solver_->smartConfidantExist(stage.sender)) {
    return;
  }

  if (stage.sRoundNum != solver_->smartRoundNumber()) {
    cswarning() << "Bad Smart's RoundNumber";
    return;
  }

  const cs::ConfidantsKeys& smartConfidants = solver_->smartConfidants();
  const cs::PublicKey& confidant = smartConfidants.at(stage.sender);

  if (!cscrypto::VerifySignature(stage.signature, confidant, bytes.data(), bytes.size())) {
    csdebug() << "Stage Two from T[" << static_cast<int>(stage.sender) << "] -  WRONG SIGNATURE!!!";
    return;
  }

  smartStageThreeMessage_[stage.sender] = std::move(bytes);

  csdebug() << "NODE> stage-3 from T[" << static_cast<int>(stage.sender) << "] is OK!";
  solver_->addSmartStageThree(stage, false);
}

void Node::smartStageRequest(MsgTypes msgType, uint8_t respondent, uint8_t required) {
  if (solver_->ownSmartsConfidantNumber() == cs::ConfidantConsts::InvalidConfidantIndex) {
    csdebug() << "NODE> Only confidant nodes can send smart-contract consensus stages";
    return;
  }

  if (!solver_->smartConfidantExist(respondent)) {
    return;
  }

  const cs::ConfidantsKeys& smartConfidants = solver_->smartConfidants();
  const cs::PublicKey& confidant = smartConfidants.at(respondent);
  sendDefault(confidant, msgType, roundNumber_, myConfidantIndex_, required);
  csmeta(csdetails) << "done";
}

void Node::getSmartStageRequest(const MsgTypes msgType, const uint8_t* data, const size_t size, const cs::PublicKey& requester) {
  csmeta(csdetails) << "started";

  if (solver_->ownSmartsConfidantNumber() == cs::ConfidantConsts::InvalidConfidantIndex) {
    cswarning() << "NODE> Only confidant nodes can send smart-contract consensus stages";
    return;
  }

  istream_.init(data, size);

  uint8_t requesterNumber = 0;
  istream_ >> requesterNumber;

  if (requester != solver_->smartConfidants().at(requesterNumber)) {
    return;
  }

  uint8_t requiredNumber = 0;
  istream_ >> requiredNumber;

  if (!solver_->smartConfidantExist(requiredNumber)) {
    return;
  }

  if (!istream_.good() || !istream_.end()) {
    cserror() << "Bad StageThree packet format";
    return;
  }

  solver_->gotSmartStageRequest(msgType, requesterNumber, requiredNumber);
}

void Node::sendSmartStageReply(const uint8_t sender, const cs::Signature& signature, const MsgTypes msgType, const uint8_t requester) {
  csmeta(csdetails) << "started";

  if (solver_->ownSmartsConfidantNumber() == cs::ConfidantConsts::InvalidConfidantIndex) {
    cswarning() << "NODE> Only confidant nodes can send smart-contract consensus stages";
    return;
  }

  if (!solver_->smartConfidantExist(requester)) {
    return;
  }

  cs::Bytes message;

  switch (msgType) {
  case MsgTypes::FirstSmartStage:
    message = smartStageOneMessage_[sender];
    break;
  case MsgTypes::SecondSmartStage:
    message = smartStageTwoMessage_[sender];
    break;
  case MsgTypes::ThirdSmartStage:
    message = smartStageThreeMessage_[sender];
    break;
  default:
    break;
  }

  sendDefault(solver_->smartConfidants().at(requester), msgType, solver_->smartRoundNumber(), signature, message);
  csmeta(csdetails) << "done";
}

void Node::prepareMetaForSending(cs::RoundTable& roundTable, std::string timeStamp, cs::StageThree& st3) {
  csmeta(csdetails) << " timestamp = " << timeStamp;

  // only for new consensus
  cs::PoolMetaInfo poolMetaInfo;
  poolMetaInfo.sequenceNumber = blockChain_.getLastSequence() + 1;  // change for roundNumber
  poolMetaInfo.timestamp = timeStamp;

  cs::Conveyer& conveyer = cs::Conveyer::instance();
  const cs::ConfidantsKeys& confidants = conveyer.confidants();
  if (st3.sender!=cs::ConfidantConsts::InvalidConfidantIndex) {
    poolMetaInfo.writerKey = conveyer.confidants().at(st3.writer);
  }

  poolMetaInfo.previousHash = blockChain_.getLastHash();

  /////////////////////////////////////////////////////////////////////////// preparing block meta info

  std::optional<csdb::Pool> pool = conveyer.applyCharacteristic(poolMetaInfo);

  if (!pool.has_value()) {
    cserror() << "NODE> applyCharacteristic() failed to create block";
    return;
  }

  pool.value().set_confidants(confidants);
  pool = blockChain_.createBlock(pool.value());

  if (!pool.has_value()) {
    cserror() << "NODE> blockchain failed to write new block";
    return;
  }

  stat_.totalAcceptedTransactions_ += pool.value().transactions_count();
  // array
  const auto lastHash = blockChain_.getLastHash().to_binary();
  std::copy(lastHash.cbegin(), lastHash.cend(), st3.blockHash.begin());
  cscrypto::GenerateSignature(st3.blockSignature, solver_->getPrivateKey(),st3.blockHash.data(),st3.blockHash.size());
  //csdebug() << "Hash acquired:"; 
  //csdebug() << "Last Hash to stage3 = " << cs::Utils::byteStreamToHex(st3.blockHash.data(), st3.blockHash.size());  
  //csdebug() << "Signature in stage3 = " << cs::Utils::byteStreamToHex(st3.blockSignature.data(), st3.blockSignature.size());

  //pool.value().sign(solver_->getPrivateKey());
  //const auto& signature = pool.value().signature();
  //std::copy(signature.cbegin(), signature.cend(), st3.blockSignature.begin());

  //logPool(pool.value());
  prepareRoundTable(roundTable, poolMetaInfo, st3);
}

void Node::addRoundSignature(const cs::StageThree& st3) {
  lastSentSignatures_.poolSignatures.push_back(cs::SignaturePair(st3.sender, st3.blockSignature));
  lastSentSignatures_.roundSignatures.push_back(cs::SignaturePair(st3.sender, st3.roundSignature));

  csdebug() << "Adding signatures of stage3 from T(" << cs::numeric_cast<int>(st3.sender)
            << ") = " << lastSentSignatures_.roundSignatures.size();
}

void Node::sendRoundPackage(const cs::PublicKey& target) {
  csmeta(csdetails) << "Send round table";
  sendDefault(target, MsgTypes::RoundTable, roundNumber_, subRound_, lastRoundTableMessage_, lastSignaturesMessage_);

  if (!lastSentRoundData_.characteristic.mask.empty()) {
    csmeta(csdebug) << "Packing " << lastSentRoundData_.characteristic.mask.size() << " bytes of char. mask to send";
  }
}

void Node::sendRoundPackageToAll() {
  //add signatures// blockSignatures, roundSignatures);
  csmeta(csdetails) << "Send round table to all";

  //bytes.reserve(stageSize);
  lastSignaturesMessage_.clear();

  cs::DataStream stream(lastSignaturesMessage_);
  stream << lastSentSignatures_.poolSignatures;
  stream << lastSentSignatures_.roundSignatures;

  csdebug() << "Send Signatures amount = " << lastSentSignatures_.roundSignatures.size();
  csdebug() << "Send RoundData : " << cs::Utils::byteStreamToHex(lastRoundTableMessage_.data(), lastRoundTableMessage_.size())
  << cs::Utils::byteStreamToHex(lastSignaturesMessage_.data(), lastSignaturesMessage_.size());
  
  sendBroadcast(MsgTypes::RoundTable, roundNumber_, subRound_, lastRoundTableMessage_, lastSignaturesMessage_);

  if (!lastSentRoundData_.characteristic.mask.empty()) {
    csmeta(csdebug) << "Packing " << lastSentRoundData_.characteristic.mask.size() << " bytes of char. mask to send";
  }
  /////////////////////////////////////////////////////////////////////////// screen output
  cslog() << "------------------------------------------  SendRoundTable  ---------------------------------------";
  cs::Conveyer& conveyer = cs::Conveyer::instance();
  auto table = conveyer.roundTable(roundNumber_);
  cslog() << "Round " << roundNumber_ << ", Confidants count " << table->confidants.size();
  cslog() << "Hashes count: " << table->hashes.size();

  transport_->clearTasks();
  onRoundStart(*table);

  // writer sometimes could not have all hashes, need check
  reviewConveyerHashes();
}

void Node::sendRoundTable() {
  becomeWriter();
  cs::Conveyer& conveyer = cs::Conveyer::instance();
  roundNumber_ = lastSentRoundData_.roundTable.round;
  subRound_ = 0;
  cs::RoundTable table;
  table.round = roundNumber_;
  table.confidants = lastSentRoundData_.roundTable.confidants;
  table.hashes = lastSentRoundData_.roundTable.hashes;
  conveyer.setRound(std::move(table));
  sendRoundPackageToAll();
}

void Node::storeRoundPackageData(const cs::RoundTable& newRoundTable, const cs::PoolMetaInfo& poolMetaInfo,
                                 const cs::Characteristic& characteristic, cs::StageThree& st3) {
  lastSentRoundData_.roundTable.round = newRoundTable.round;
  lastSentRoundData_.subRound = subRound_;
  // no general stored!
  lastSentRoundData_.roundTable.confidants.clear();
  lastSentRoundData_.roundTable.confidants = newRoundTable.confidants;

  lastSentRoundData_.roundTable.hashes.clear();
  lastSentRoundData_.roundTable.hashes = newRoundTable.hashes;

  lastSentRoundData_.characteristic.mask.clear();
  lastSentRoundData_.characteristic.mask = characteristic.mask;

  lastSentRoundData_.poolMetaInfo.sequenceNumber = poolMetaInfo.sequenceNumber;
  lastSentRoundData_.poolMetaInfo.timestamp = poolMetaInfo.timestamp;
  lastSentRoundData_.poolMetaInfo.writerKey = poolMetaInfo.writerKey;
  lastSentRoundData_.poolMetaInfo.previousHash = poolMetaInfo.previousHash;
  lastSentRoundData_.poolMetaInfo.realTrustedMask = st3.realTrustedMask;

  size_t expectedMessageSize = newRoundTable.confidants.size() * sizeof(cscrypto::PublicKey) + sizeof(size_t)
                             + newRoundTable.hashes.size() * sizeof(cscrypto::Hash) + sizeof(size_t)
                             + poolMetaInfo.timestamp.size() * sizeof(cs::Byte) + sizeof(size_t)
                             + characteristic.mask.size() * sizeof(cs::Byte) + sizeof(size_t)
                             + sizeof(size_t) 
                             + sizeof(cs::Hash) + sizeof(size_t) 
                             + poolMetaInfo.realTrustedMask.size() + sizeof(size_t);

  //cs::Bytes messageToSign;
  //messageToSign.reserve(sizeof(cs::RoundNumber) + sizeof(uint8_t) + sizeof(cs::Hash));
  lastRoundTableMessage_.clear();
  lastRoundTableMessage_.reserve(expectedMessageSize);
  cs::DataStream stream(lastRoundTableMessage_);
  stream << lastSentRoundData_.roundTable.confidants;
  stream << lastSentRoundData_.roundTable.hashes;
  stream << lastSentRoundData_.poolMetaInfo.timestamp;
  stream << lastSentRoundData_.characteristic.mask;
  stream << lastSentRoundData_.poolMetaInfo.sequenceNumber;
  stream << lastSentRoundData_.poolMetaInfo.realTrustedMask;
  stream << lastSentRoundData_.poolMetaInfo.previousHash;
  //stream << lastSentRoundData_.poolMetaInfo.writerKey; -- we don't need to send this

  cscrypto::CalculateHash(st3.roundHash, lastRoundTableMessage_.data(), lastRoundTableMessage_.size());
  //cs::DataStream signStream(messageToSign);
  //signStream << roundNumber_;
  //signStream << subRound_;
  //signStream << st3.roundHash;
  cscrypto::GenerateSignature(st3.roundSignature, solver_->getPrivateKey(), st3.roundHash.data(), st3.roundHash.size());

  //csdebug() << "Round Signature calculated:";
  //csdebug() << "Last Hash to stage3 = " << cs::Utils::byteStreamToHex(st3.roundHash.data(), st3.roundHash.size());
  //csdebug() << "Signature in stage3 = " << cs::Utils::byteStreamToHex(st3.roundSignature.data(), st3.roundSignature.size());

  //here should be placed parcing of round table
  lastSentSignatures_.poolSignatures.clear();
  lastSentSignatures_.roundSignatures.clear();
}

void Node::prepareRoundTable(cs::RoundTable& roundTable, const cs::PoolMetaInfo& poolMetaInfo, cs::StageThree& st3) {
  cs::Conveyer& conveyer = cs::Conveyer::instance();

  const cs::Characteristic* block_characteristic = conveyer.characteristic(conveyer.currentRoundNumber());

  if (!block_characteristic) {
    csmeta(cserror) << "Send round info characteristic not found, logic error";
    return;
  }

  stat_.totalReceivedTransactions_ += block_characteristic->mask.size();

  storeRoundPackageData(roundTable, poolMetaInfo, *block_characteristic, st3); 
  csdebug() << "StageThree prepared:";
  st3.print();
}

void Node::getRoundTable(const uint8_t* data, const size_t size, const cs::RoundNumber rNum, const cs::PublicKey& sender) {
  csdebug() << "NODE> next round table received, round: " << rNum;
  csmeta(csdetails) << "started";

  if (myLevel_ == NodeLevel::Writer) {
    csmeta(cserror) << "Writers don't receive round table";
    return;
  }

  csdebug() << "Got RoundData : " << cs::Utils::byteStreamToHex(data,size);
  istream_.init(data, size);

  // RoundTable evocation
  uint8_t subRound = 0;
  istream_ >> subRound;

  if (rNum == roundNumber_ && subRound_ > subRound) {
    cswarning() << "NODE> round table SUBROUND is lesser then local one, ignore round table";
    return;
  }

  subRound_ = subRound;
  cs::Bytes roundBytes;
  istream_ >> roundBytes;

  cs::Bytes bytes;
  istream_ >> bytes;

  cs::DataStream stream(bytes.data(), bytes.size());

  std::vector<cs::SignaturePair> poolSignatures;
  stream >> poolSignatures;
  csdebug() << "PoolSignatures Amount = " << poolSignatures.size();

  std::vector<cs::SignaturePair> roundSignatures;
  stream >> roundSignatures;

  csdebug() << "RoundSignatures Amount = " << roundSignatures.size();

  size_t signaturesCount = 0;
  auto rt = cs::Conveyer::instance().roundTable(rNum - 1);
  if (rt == nullptr) {
    return;
  }
  for (auto& it : roundSignatures) {
    cs::Hash tempHash;
    cscrypto::CalculateHash(tempHash, roundBytes.data(), roundBytes.size());
    if(cscrypto::VerifySignature(it.signature,rt->confidants.at(it.sender), tempHash.data(), tempHash.size())) {
      ++signaturesCount;
    }
  }
  size_t neededConfNumber = rt->confidants.size()/2U +1U;
  if (signaturesCount == roundSignatures.size() && signaturesCount >= neededConfNumber) {
    csdebug() << "All signatures in RoundTable are ok!";
  } 
  else {
    csdebug() << "RoundTable is not valid!";
    return;
  }

  cs::DataStream roundStream(roundBytes.data(), roundBytes.size());
  cs::ConfidantsKeys confidants;
  roundStream >> confidants;

  if (confidants.empty()) {
    csmeta(cserror) << "Illegal confidants count in round table";
    return;
  }

  cs::PacketsHashes hashes;
  roundStream >> hashes;

  cs::RoundTable roundTable;
  roundTable.round = rNum;
  roundTable.confidants = std::move(confidants);
  roundTable.hashes = std::move(hashes);
  roundTable.general = sender;
  csdebug() << "NODE> confidants: " << roundTable.confidants.size();

  // create pool by previous round, then change conveyer state.
  cs::Conveyer& conveyer = cs::Conveyer::instance();

  getCharacteristic(reinterpret_cast<cs::Byte*>(roundStream.data()), roundStream.size(),
                    conveyer.currentRoundNumber(), sender, std::move(poolSignatures));
  conveyer.setRound(std::move(roundTable));

  onRoundStart(conveyer.currentRoundTable());
  poolSynchronizer_->processingSync(roundNumber_);
  reviewConveyerHashes();

  csmeta(csdetails) << "done\n";
}

void Node::sendHash(cs::RoundNumber round) {
#if !defined(MONITOR_NODE) && !defined(WEB_WALLET_NODE)
  if (blockChain_.getLastSequence() != round - 1) {
    // should not send hash until have got proper block sequence
    return;
  }

  const auto& hash = blockChain_.getLastHash();
  csdb::PoolHash spoiledHash;
  csdebug() << "NODE> Sending hash " << hash.to_string() << " to ALL";
  spoiledHash = spoileHash(hash, solver_->getPublicKey());
  sendToConfidants(MsgTypes::BlockHash, round, subRound_, spoiledHash);
  csdebug() << "NODE> Hash sent, round: " << round << "." << cs::numeric_cast<int>(subRound_);
#endif
}

void Node::getHash(const uint8_t* data, const size_t size, cs::RoundNumber rNum, const cs::PublicKey& sender) {
  if (myLevel_ != NodeLevel::Confidant) {
    csdebug() << "NODE> ignore hash as no confidant";
    return;
  }

  csdetails() << "NODE> get hash of round " << rNum << ", data size " << size;

  istream_.init(data, size);
  uint8_t subRound;
  istream_ >> subRound;

  if (subRound > subRound_) {
    cswarning() << "NODE> We got hash for the Node with SUBROUND: " << subRound << ", we don't have";
    // TODO : Maybe return
  }

  csdb::PoolHash tmp;
  istream_ >> tmp;

  if (!istream_.good() || !istream_.end()) {
    cswarning() << "NODE> bad hash packet format";
    return;
  }

  //csdb::PoolHash lastHash = blockChain_.getLastHash();
  //csdb::PoolHash spoiledHash = spoileHash(lastHash, sender);

  //if (spoiledHash == tmp) {
  solver_->gotHash(std::move(tmp), sender);
  //} 
  //else {
  //  cswarning() << "NODE> Hash from: " << cs::Utils::byteStreamToHex(sender.data(), sender.size())
  //              << " DOES NOT MATCH to my value " << lastHash.to_string();

  //}
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
  csdebug() << "NODE> send request for next round info after #" << roundNumber_;

  // ask for next round info:
  sendDefault(respondent, MsgTypes::RoundTableRequest, roundNumber_, myConfidantIndex_);
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

  if (myLevel_ != NodeLevel::Confidant) {
    csdebug() << "Only confidant nodes can reply consensus stages";
  }

  sendDefault(target, MsgTypes::RoundTableReply, roundNumber_, hasRequestedInfo);
}

bool Node::tryResendRoundTable(const cs::PublicKey& target, const cs::RoundNumber rNum) {
  if (lastSentRoundData_.roundTable.round != rNum || lastSentRoundData_.subRound != subRound_) {
    csdebug() << "NODE> unable to repeat round data #" << rNum;
    return false;
  }

  csdebug() << "NODE> Re-send last round info #" << rNum << " to ALL";

  sendRoundPackage(target);

  return true;
}

void Node::getRoundTableReply(const uint8_t* data, const size_t size, const cs::PublicKey& respondent) {
  csmeta(csdetails);

  if (myLevel_ != NodeLevel::Confidant) {
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
  roundNumber_ = roundTable.round;
  bool found = false;
  uint8_t confidantIndex = 0;

  for (auto& conf : roundTable.confidants) {
    if (conf == nodeIdKey_) {
      myLevel_ = NodeLevel::Confidant;
      myConfidantIndex_ = confidantIndex;
      found = true;
      break;
    }

    confidantIndex++;
  }

  if (!found) {
    myLevel_ = NodeLevel::Normal;
  }
  // TODO: think how to improve this code.
  stageOneMessage_.clear();
  stageOneMessage_.resize(roundTable.confidants.size());
  stageTwoMessage_.clear();
  stageTwoMessage_.resize(roundTable.confidants.size());
  stageThreeMessage_.clear();
  stageThreeMessage_.resize(roundTable.confidants.size());
  stageThreeSent = false;
  constexpr int padWidth = 30;

  std::ostringstream line1;
  for (int i = 0; i < padWidth; i++) {
    line1 << '=';
  }

  line1 << " " << roundNumber_ << "." << cs::numeric_cast<int>(subRound_) << " ";

  if (NodeLevel::Normal == myLevel_) {
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
  const std::size_t fixed_width = s.size();

  cslog() << s;
  csdebug() << " Node key " << cs::Utils::byteStreamToHex(nodeIdKey_.data(), nodeIdKey_.size());
  cslog() << " last written sequence = " << blockChain_.getLastSequence();

  std::ostringstream line2;

  for (std::size_t i = 0; i < fixed_width; ++i) {
    line2 << '-';
  }

  csdebug() << line2.str();

  csdebug() << " Confidants:";

  for (size_t i = 0; i < roundTable.confidants.size(); ++i) {
    const auto& confidant = roundTable.confidants[i];
    auto result = myLevel_ == NodeLevel::Confidant && i == myConfidantIndex_;
    auto name =  result ? "me" : cs::Utils::byteStreamToHex(confidant.data(), confidant.size());

    csdebug() << "[" << i << "] " << name;
  }

  csdebug() << " Hashes: " << roundTable.hashes.size();

  for (size_t j = 0; j < roundTable.hashes.size(); ++j) {
    const auto& hashBinary = roundTable.hashes[j].toBinary();
    csdetails() << "[" << j << "] " << cs::Utils::byteStreamToHex(hashBinary.data(), hashBinary.size());
  }

  csdebug() << line2.str();
  stat_.onRoundStart(roundNumber_);
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
  if (roundNumber == blockChain_.getLastSequence() + 1) {
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
  cs::Hash hash;
  cscrypto::CalculateHash(hash, hashToSpoil.to_binary().data(), hash.size(), reinterpret_cast<cs::Byte*>(roundNumber_), sizeof(cs::RoundNumber));
  cs::Bytes bytesHash(hash.begin(), hash.end());

  return csdb::PoolHash::from_binary(std::move(bytesHash));
}

csdb::PoolHash Node::spoileHash(const csdb::PoolHash& hashToSpoil, const cs::PublicKey& pKey) {
  cs::Hash hash;
  cscrypto::CalculateHash(hash, hashToSpoil.to_binary().data(), hash.size(), pKey.data(), pKey.size());
  cs::Bytes bytesHash(hash.begin(), hash.end());

  return csdb::PoolHash::from_binary(std::move(bytesHash));
}

void Node::smartStageEmptyReply(uint8_t requesterNumber) {
  csunused(requesterNumber);
  csdebug() << "Here should be the smart refusal for the SmartStageRequest";
}

void Node::sendHashReply(const csdb::PoolHash& hash, const cs::PublicKey& respondent) {
  csmeta(csdebug);
  if (myLevel_ != NodeLevel::Confidant) {
    csmeta(csdebug) << "Only confidant nodes can send hash reply to other nodes";
    return;
  }

  cs::Signature signature;
  cscrypto::GenerateSignature(signature, solver_->getPrivateKey(), hash.to_binary().data(), hash.size());
  sendDefault(respondent, MsgTypes::HashReply, roundNumber_, subRound_, signature, hash);
}

void Node::getHashReply(const uint8_t* data, const size_t size, cs::RoundNumber rNum, const cs::PublicKey& sender) {
  static size_t badHashReplyCounter = 0;

  istream_.init(data, size);
  uint8_t subRound = 0;
  istream_ >> subRound;

  if (roundNumber_ != rNum || subRound_ != subRound) {
    csdebug() << "NODE> Get hash reply on incorrect round: " << rNum << "(" << subRound << ")";
    return;
  }

  cs::Signature signature;
  istream_ >> signature;

  csdb::PoolHash hash;
  istream_ >> hash;

  const auto& confidants = cs::Conveyer::instance().confidants();
  if (std::find(confidants.cbegin(), confidants.cend(), sender) == confidants.cend()) {
    csmeta(csdebug) << "The message of WRONG HASH was sent by false confidant!";
    return;
  }

  if (std::find(confidants.cbegin(), confidants.cend(), sender) != confidants.cend()) {
    if (!cscrypto::VerifySignature(signature, sender, hash.to_binary().data(), hash.size())) {
      csmeta(csdebug) << "The message of WRONG HASH has WRONG SIGNATURE!";
      return;
    }
  }
  else {
    return;
  }

  ++badHashReplyCounter;
  if (badHashReplyCounter > confidants.size() / 2) {
    csmeta(csdebug) << "This node really have not valid HASH!!! Removing last block from DB and trying to syncronize";
    badHashReplyCounter = 0;
    blockChain_.removeLastBlock();
  }
}
