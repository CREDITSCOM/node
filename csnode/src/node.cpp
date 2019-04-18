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
#include <csnode/poolsynchronizer.hpp>
#include <csnode/nodeutils.hpp>

#include <lib/system/logger.hpp>
#include <lib/system/utils.hpp>
#include <lib/system/progressbar.hpp>
#include <lib/system/signals.hpp>

#include <net/transport.hpp>

#include <base58.h>

#include <boost/optional.hpp>

#include <lz4.h>
#include <cscrypto/cscrypto.hpp>

const unsigned MIN_CONFIDANTS = 3;
const unsigned MAX_CONFIDANTS = 100;

const csdb::Address Node::genesisAddress_ = csdb::Address::from_string("0000000000000000000000000000000000000000000000000000000000000001");
const csdb::Address Node::startAddress_ = csdb::Address::from_string("0000000000000000000000000000000000000000000000000000000000000002");

bool Node::stopRequested_ = false;

Node::Node(const Config& config)
: nodeIdKey_(config.getMyPublicKey())
, nodeIdPrivate_(config.getMyPrivateKey())
, blockChain_(genesisAddress_, startAddress_)
, allocator_(1 << 24, 5)
, packStreamAllocator_(1 << 26, 5)
, ostream_(&packStreamAllocator_, nodeIdKey_)
, stat_() {

  std::fill(ssKey_.begin(), ssKey_.end(), 0);

  solver_ = new cs::SolverCore(this, genesisAddress_, startAddress_);
  std::cout << "Start transport... ";
  transport_ = new Transport(config, this);
  std::cout << "Done\n";
  poolSynchronizer_ = new cs::PoolSynchronizer(config.getPoolSyncSettings(), transport_, &blockChain_);

  cs::Connector::connect(blockChain_.getStorage().read_block_event(), &stat_, &cs::RoundStat::onReadBlock);
  cs::Connector::connect(&blockChain_.storeBlockEvent, &stat_, &cs::RoundStat::onStoreBlock);
  cs::Connector::connect(&blockChain_.storeBlockEvent, &executor::Executor::getInstance(&blockChain_, solver_, config.getApiSettings().executorPort), &executor::Executor::onBlockStored);
  cs::Connector::connect(&transport_->pingReceived, this, &Node::onPingReceived);

#ifdef NODE_API
  std::cout << "Init API... ";
  api_ = std::make_unique<csconnector::connector>(blockChain_, solver_,
    csconnector::Config {
     config.getApiSettings().port,
     config.getApiSettings().ajaxPort,
     config.getApiSettings().executorPort,
     config.getApiSettings().apiexecPort
    });
  std::cout << "Done\n";
  cs::Connector::connect(blockChain_.getStorage().read_block_event(), api_.get(), &csconnector::connector::onReadFromDB);
  cs::Connector::connect(&blockChain_.storeBlockEvent, api_.get(), &csconnector::connector::onStoreBlock);
#endif // NODE_API

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
  cslog() << "Blockchain is ready, contains " << stat_.total_transactions() << " transactions";

#ifdef NODE_API
  api_->run();
#endif // NODE_API

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
  solver_->startDefault();

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
  static std::map<cs::RoundNumber, uint8_t> recdBangs;
  auto& conveyer = cs::Conveyer::instance();

  cswarning() << "-----------------------------------------------------------";
  cswarning() << "NODE> BigBang #" << rNum
              << ": last written #" << blockChain_.getLastSequence()
              << ", current #" << conveyer.currentRoundNumber();
  cswarning() << "-----------------------------------------------------------";

  istream_.init(data, size);
  istream_ >> subRound_;

  if (subRound_ <= recdBangs[rNum]) {
    cswarning() << "Old Big Bang received: " << rNum << "." << static_cast<int>(subRound_) << " is <= " << rNum << "." << static_cast<int>(recdBangs[rNum]);
    return;
  }

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

  // this evil code sould be removed after examination
  cs::Sequence countRemoved = 0;
  cs::Sequence lastSequence = blockChain_.getLastSequence();

  while (lastSequence >= rNum) {
    if (countRemoved == 0) {
      // the 1st time
      csdebug() << "NODE> remove " << lastSequence - rNum << " block(s) required (rNum = " << rNum << ", last_seq = " << lastSequence << ")";
    }

    blockChain_.removeLastBlock();
    cs::RoundNumber tmp = blockChain_.getLastSequence();

    if (lastSequence == tmp) {
      csdebug() << "NODE> cancel remove blocks operation (last removal is failed)";
      break;
    }

    ++countRemoved;
    lastSequence = tmp;
  }

  if (countRemoved > 0) {
    csdebug() << "NODE> " << countRemoved << " block(s) was removed";
  }

  // resend all this round data available
  csdebug() << "NODE> resend last block hash after BigBang";

  globalTable.hashes = conveyer.currentRoundTable().hashes;

  csmeta(csdebug) << "Get BigBang globalTable.hashes: " << globalTable.hashes.size();

  onRoundStart(globalTable);
  conveyer.updateRoundTable(cachedRound, globalTable);

  poolSynchronizer_->sync(globalTable.round, cs::PoolSynchronizer::roundDifferentForSync, true);

  if (conveyer.isSyncCompleted()) {
    startConsensus();
  }
  else {
    sendPacketHashesRequest(conveyer.currentNeededHashes(), conveyer.currentRoundNumber(), startPacketRequestPoint_);
  }
}

void Node::getKeySS(const cs::PublicKey& key)
{
  std::copy(key.cbegin(), key.cend(), ssKey_.begin());
  cslog() << "Node: SS registration key " << cs::Utils::byteStreamToHex(ssKey_.data(), ssKey_.size()) << " ("
    << EncodeBase58(ssKey_.data(), ssKey_.data() + ssKey_.size()) << ')';
}

void Node::getRoundTableSS(const uint8_t* data, const size_t size, const cs::RoundNumber rNum) {
  istream_.init(data, size);
  if (!(cs::Conveyer::instance().currentRoundNumber() == 0 && rNum == 1)) {
    csdebug() << "The RoundTable sent by SS doesn't correspond to the current RoundNumber";
    return;
  }
  cslog() << "NODE> get SS Round Table #" << rNum;
  cs::RoundTable roundTable;

  if (!readRoundData(roundTable, false)) {
    cserror() << "NODE> read round data from SS failed, continue without round table";
  }

  // update new round data from SS
  // TODO: fix sub round
  subRound_ = 0;
  roundTable.round = rNum;

  cs::Conveyer::instance().setRound(rNum);
  cs::Conveyer::instance().setTable(roundTable);

  // "normal" start
  if (roundTable.round == 1) {
  //  cs::Timer::singleShot(TIME_TO_AWAIT_SS_ROUND, cs::RunPolicy::CallQueuePolicy, [this, roundTable]() {
      onRoundStart(roundTable);
      reviewConveyerHashes();
 //   });

    return;
  }

  // "hot" start
  handleRoundMismatch(roundTable);
}

// handle mismatch between own round & global round, calling code should detect mismatch before calling to the method
void Node::handleRoundMismatch(const cs::RoundTable& globalTable) {
  csmeta(csdetails) << "round: " << globalTable.round;
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

    poolSynchronizer_->sync(globalTable.round);
    // no return, ask for next round info
  }
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
    stopRequested_ = true;
  });
}

bool Node::canBeTrusted() {

#if defined(MONITOR_NODE) || defined(WEB_WALLET_NODE)

  return false;

#else

  if (stopRequested_) {
    return false;
  }

  if (Consensus::DisableTrustedRequestNextRound) {
    // ignore flag after bigbang
    if (myLevel_ == Level::Confidant && subRound_ == 0) {
      return false;
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
  return true;

#endif
}

void Node::getPacketHashesRequest(const uint8_t* data, const std::size_t size, const cs::RoundNumber round, const cs::PublicKey& sender) {
  //csdebug() << __func__ << ": start";
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
 // csdebug() << __func__ << ": done";
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

void Node::getCharacteristic(const uint8_t* data, const size_t size, const cs::RoundNumber round,
                             const cs::PublicKey& sender, cs::Signatures&& poolSignatures, cs::Bytes&& realTrusted) {
  csmeta(csdetails) << "started";
  cs::Conveyer& conveyer = cs::Conveyer::instance();

  if (!conveyer.isSyncCompleted(round)) {
    csdebug() << "NODE> Packet sync not finished, saving characteristic meta to call after sync";

    cs::Bytes characteristicBytes(data, data + size);

    cs::CharacteristicMeta meta;
    meta.bytes = std::move(characteristicBytes);
    meta.sender = sender;
    meta.signatures = std::move(poolSignatures);
    meta.realTrusted = std::move(realTrusted);

    conveyer.addCharacteristicMeta(round, std::move(meta));
    return;
  }

  cs::DataStream poolStream(data, size);
  cs::Characteristic characteristic;
  cs::PoolMetaInfo poolMetaInfo;
  size_t smartSigCount;
  csdb::Pool::SmartSignature tmpSmartSignature;

  poolStream >> poolMetaInfo.timestamp;
  poolStream >> characteristic.mask;
  poolStream >> poolMetaInfo.sequenceNumber;
  poolStream >> poolMetaInfo.previousHash;
  poolStream >> smartSigCount;
  poolMetaInfo.realTrustedMask = realTrusted;

  if (myLevel_ == Level::Confidant) {
    csdebug() << "We probably don't have enough confirmations so we try to throw our last deferred block";
    solver_->removeDeferredBlock(poolMetaInfo.sequenceNumber);
  }

  csdebug() << "Trying to get confidants from round " << round;
  const auto table = conveyer.roundTable(round);

  if (table == nullptr) {
    cserror() << "NODE> cannot access proper round table to add trusted to pool #" << poolMetaInfo.sequenceNumber;
    return;
  }

  const cs::ConfidantsKeys& confidantsReference = table->confidants;
  const std::size_t realTrustedMaskSize = poolMetaInfo.realTrustedMask.size();

  csdebug() << "Real TrustedMask size = " << realTrustedMaskSize;

  if (realTrustedMaskSize > confidantsReference.size()) {
    csmeta(cserror) << ", real trusted mask size: " << realTrustedMaskSize
                    << ", confidants count " << confidantsReference.size()
                    << ", on round " << round;
    return;
  }

  for (size_t idx = 0; idx < realTrustedMaskSize; ++idx) {
    const auto& key = confidantsReference[idx];

    if (poolMetaInfo.realTrustedMask[idx] == 0) {
      poolMetaInfo.writerKey = key;
    }
  }

  if (round != 0) {
    auto confirmation = confirmationList_.find(round);
    if (confirmation.has_value()) {
      poolMetaInfo.confirmationMask = confirmation.value().mask;
      poolMetaInfo.confirmations = confirmation.value().signatures;
    }
  }

  if (!istream_.good()) {
    csmeta(cserror) << "Round info parsing failed, data is corrupted";
    return;
  }

  csdebug() << "NODE> Sequence " << poolMetaInfo.sequenceNumber << ", mask size " << characteristic.mask.size();
  csdebug() << "NODE> Time: " << poolMetaInfo.timestamp;

  if (blockChain_.getLastSequence() > poolMetaInfo.sequenceNumber) {
    csmeta(cswarning) << "blockChain last seq: " << blockChain_.getLastSequence()
                      << " > pool meta info seq: " << poolMetaInfo.sequenceNumber;
    return;
  }

  // otherwise senseless, this block is already in chain
  conveyer.setCharacteristic(characteristic, poolMetaInfo.sequenceNumber);
  std::optional<csdb::Pool> pool = conveyer.applyCharacteristic(poolMetaInfo);

  if (!pool.has_value()) {
    csmeta(cserror) << "Created pool is not valid";
    return;
  }

  pool.value().set_signatures(poolSignatures);
  pool.value().set_confidants(confidantsReference);

  if (!blockChain_.storeBlock(pool.value(), false /*by_sync*/)) {
    cserror() << "NODE> failed to store block in BlockChain";
  }
  else {
    blockChain_.testCachedBlocks();
    confirmationList_.remove(round);
  }

  csmeta(csdetails) << "done";
}

void Node::cleanConfirmationList(cs::RoundNumber rNum) {
  confirmationList_.remove(rNum);
}

cs::ConfidantsKeys Node::retriveSmartConfidants(const cs::Sequence startSmartRoundNumber) const {
  csmeta(csdebug);
  //возможна ошибка если на пишущем узле происходит запись блока в конце предыдущего раунда, а в других нодах в начале
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
  //csdebug() << __func__ << ": start";
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
  //csdebug() << __func__ << ": done";
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

  csdebug() << "Node> Sending blocks with signatures:";
  for (const auto& it : poolsBlock) {
    csdebug() << "#" << it.sequence() << " signs = " << it.signatures().size();
  }

  std::size_t realBinSize = 0;
  RegionPtr memPtr = compressPoolsBlock(poolsBlock, realBinSize);

  tryToSendDirect(target, MsgTypes::RequestedBlock, cs::Conveyer::instance().currentRoundNumber(), realBinSize,
                  cs::numeric_cast<uint32_t>(memPtr.size()), memPtr, packetNum);
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
  //csdebug() << __func__ << ": done";
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
      getCharacteristic(meta->bytes.data(), meta->bytes.size(), round, meta->sender, std::move(meta->signatures), std::move(meta->realTrusted));
    }

    // if next block maybe stored, the last written sequence maybe updated, so deferred consensus maybe resumed
    if (blockChain_.getLastSequence() + 1 == cs::Conveyer::instance().currentRoundNumber()) {
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

void Node::onPingReceived(cs::Sequence sequence) {
  static std::chrono::steady_clock::time_point point = std::chrono::steady_clock::now();
  static std::chrono::milliseconds delta{0};
  static cs::Sequence maxSequence = 0;

  auto now = std::chrono::steady_clock::now();
  delta += std::chrono::duration_cast<std::chrono::milliseconds>(now - point);

  if (maxSequence < sequence) {
    maxSequence = sequence;
    delta = std::chrono::milliseconds(0);
  }

  if (maxPingSynchroDelay_ <= delta.count()) {
    delta = std::chrono::milliseconds(0);
    auto lastSequence = blockChain_.getLastSequence();

    if (lastSequence < maxSequence) {
      cswarning() << "Last sequence is lower than network max sequence, trying to sync";

      CallsQueue::instance().insert([=] {
        cs::Conveyer::instance().setRound(maxSequence);

        auto sequenceDifference = maxSequence - lastSequence;
        poolSynchronizer_->sync(maxSequence, sequenceDifference);
      });
    }
  }

  point = now;
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

inline bool Node::readRoundData(cs::RoundTable& roundTable, bool bang) {
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
  for (int i =0; i<confSize; ++i)
  {
    cs::PublicKey key;
    istream_ >> key;

    confidants.push_back(std::move(key));

  }
  if (bang) {
    cs::Signature sig;
    istream_ >>sig;
  
    cs::Bytes trustedToHash;
    cs::DataStream tth(trustedToHash);
    tth << roundTable.round;
    tth << confidants;
    csdebug() << "Message to Sign: " << cs::Utils::byteStreamToHex(trustedToHash);
    //cs::Hash trustedHash = cscrypto::calculateHash(trustedToHash.data(), trustedToHash.size());
    csdebug() << "SSKey: " << cs::Utils::byteStreamToHex(ssKey_.data(), ssKey_.size());
    if (!cscrypto::verifySignature(sig, ssKey_, trustedToHash.data(), trustedToHash.size())) { 
      cswarning() << "The BIGBANG message is incorrect: signature isn't valid";
      return false;
    }
    cs::Bytes confMask;
    cs::Signatures signatures;
    signatures.push_back(sig);
    confMask.push_back(0);
    confirmationList_.remove(roundTable.round);
    confirmationList_.add(roundTable.round, bang, confidants, confMask, signatures);
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
  corruptionLevel_ = 0;
  if (myLevel_ != Level::Confidant) {
    cswarning() << "NODE> Only confidant nodes can send consensus stages";
    return;
  }

  stageOneInfo.roundTimeStamp = cs::Utils::currentTimestamp();

  csmeta(csdebug) << "Round: " << cs::Conveyer::instance().currentRoundNumber() << "." << cs::numeric_cast<int>(subRound_)
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
  stageOneInfo.messageHash = cscrypto::calculateHash(message.data(), message.size());

  cs::DataStream signStream(messageToSign);
  signStream << cs::Conveyer::instance().currentRoundNumber();
  signStream << subRound_;
  signStream << stageOneInfo.messageHash;

  // signature of round number + calculated hash
  stageOneInfo.signature = cscrypto::generateSignature(solver_->getPrivateKey(), messageToSign.data(), messageToSign.size());
  csdebug() << "Stage one Signature R-" << cs::Conveyer::instance().currentRoundNumber() << "(" << static_cast<int>(stageOneInfo.sender) 
      << "): " << cs::Utils::byteStreamToHex(stageOneInfo.signature.data(), stageOneInfo.signature.size());

  const int k1 = (corruptionLevel_ / 1) % 2;
  const cs::Byte k2 = static_cast<cs::Byte>(corruptionLevel_ / 16);
  if (k1 == 1 && k2 == myConfidantIndex_) {
    csdebug() << "STAGE ONE ##############> NOTHING WILL BE SENT";
  }
  else {
    //if (myConfidantIndex_ != 1) {
      sendToConfidants(MsgTypes::FirstStage, cs::Conveyer::instance().currentRoundNumber(), subRound_, stageOneInfo.signature, message);
    //}
  }

  csmeta(csdetails) << "Sent message size " << message.size();

  // cache
  stageOneInfo.message = std::move(message);
  csmeta(csdetails) << "done";
}

void Node::getStageOne(const uint8_t* data, const size_t size, const cs::PublicKey& sender) {
  //if (cs::Conveyer::instance().currentRoundNumber() % 10 == 0 && subRound_ == 0) {
  //  return;
  //}
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
  istream_ >> stage.message;

  csdetails() << "Stage1 message: " << cs::Utils::byteStreamToHex(stage.message);
  csdetails() << "Stage1 signature: " << cs::Utils::byteStreamToHex(stage.signature);


  if (!istream_.good() || !istream_.end()) {
    csmeta(cserror) << "Bad stage-1 packet format";
    return;
  }

  // hash of part received message
  stage.messageHash = cscrypto::calculateHash(stage.message.data(), stage.message.size());

  cs::Bytes signedMessage;
  cs::DataStream signedStream(signedMessage);
  signedStream << cs::Conveyer::instance().currentRoundNumber();
  signedStream << subRound_;
  signedStream << stage.messageHash;

  // stream for main message
  cs::DataStream stream(stage.message.data(), stage.message.size());
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
  stageTwoInfo.signature = cscrypto::generateSignature(solver_->getPrivateKey(), bytes.data(), bytes.size());

  const int k1 = (corruptionLevel_ / 2) % 2;
  const cs::Byte k2 = static_cast<cs::Byte>(corruptionLevel_ / 16);

  if (k1 == 1 && k2 == myConfidantIndex_) {
    csdebug() << "STAGE TWO ##############> NOTHING WILL BE SENT";
  }
  else {
    //if (myConfidantIndex_ != 1) {
    sendToConfidants(MsgTypes::SecondStage, cs::Conveyer::instance().currentRoundNumber(), subRound_, stageTwoInfo.signature, bytes);
    //}
  }

  // cash our stage two
  csmeta(csdetails) << "Bytes size " << bytes.size();

  stageTwoInfo.message = std::move(bytes);

  csmeta(csdetails) << "done";
}

void Node::getStageTwo(const uint8_t* data, const size_t size, const cs::PublicKey& sender) {
  csmeta(csdetails);

  if (myLevel_ != Level::Confidant && myLevel_ != Level::Writer) {
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
  
  if (!cscrypto::verifySignature(stage.signature, conveyer.confidantByIndex(stage.sender), bytes.data(), bytes.size())) {
    csdebug() << "NODE> stage-2 [" << static_cast<int>(stage.sender) << "] -  WRONG SIGNATURE!!!";
    return;
  }

  csmeta(csdetails) << "Signature is OK";
  stage.message = std::move(bytes);

  csdebug() << "NODE> stage-2 [" << static_cast<int>(stage.sender) << "] is OK!";
  solver_->gotStageTwo(stage);
}

void Node::sendStageThree(cs::StageThree& stageThreeInfo) {
  csdebug() << __func__;
  //csmeta(csdetails) << "started";

  if (myLevel_ != Level::Confidant) {
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
  stream << stageThreeInfo.iteration;
  stream << stageThreeInfo.blockSignature;
  stream << stageThreeInfo.roundSignature;
  stream << stageThreeInfo.trustedSignature;
  stream << stageThreeInfo.realTrustedMask;

  //cscrypto::GenerateSignature(stageThreeInfo.blockSignature, solver_->getPrivateKey(), stageThreeInfo.blockHash.data(), stageThreeInfo.blockHash.size());
  //cscrypto::GenerateSignature(stageThreeInfo.roundSignature, solver_->getPrivateKey(), stageThreeInfo.roundHash.data(), stageThreeInfo.roundHash.size());
  stageThreeInfo.signature = cscrypto::generateSignature(solver_->getPrivateKey(), bytes.data(), bytes.size());

  const int k1 = (corruptionLevel_ / 4) % 2;
  const cs::Byte k2 = static_cast<cs::Byte>(corruptionLevel_ / 16);

  //if (myConfidantIndex_ == 1 && std::count(stageThreeInfo.realTrustedMask.cbegin(), stageThreeInfo.realTrustedMask.cend(), cs::ConfidantConsts::InvalidConfidantIndex)==0) {
  if (k1 == 1 && k2 == myConfidantIndex_) {
    csdebug() << "STAGE THREE ##############> NOTHING WILL BE SENT";
  }
  else {
    sendToConfidants(MsgTypes::ThirdStage, cs::Conveyer::instance().currentRoundNumber(), subRound_, stageThreeInfo.signature, bytes);
  }

  // cach stage three
  csmeta(csdetails) << "bytes size " << bytes.size();
  stageThreeInfo.message = std::move(bytes);
  stageThreeSent = true;
  csmeta(csdetails) << "done";
}

void Node::getStageThree(const uint8_t* data, const size_t size) {
  csmeta(csdetails);

  if (myLevel_ != Level::Confidant && myLevel_ != Level::Writer) {
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
  stream >> stage.iteration;//this is a potential problem!!!
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

  if (stage.iteration < solver_->currentStage3iteration() ) {
    stageRequest(MsgTypes::ThirdStage, myConfidantIndex_, stage.sender);
    return;
  } 
  else if (stage.iteration > solver_->currentStage3iteration()) {
    //store
    return;
  }

  if (!cscrypto::verifySignature(stage.signature, conveyer.confidantByIndex(stage.sender), bytes.data(), bytes.size())) {
    cswarning() << "NODE> stage-3 from T[" << static_cast<int>(stage.sender) << "] -  WRONG SIGNATURE!!!";
    return;
  }

  stage.message = std::move(bytes);

  csdebug() << "NODE> stage-3 from T[" << static_cast<int>(stage.sender) << "] is OK!";

  solver_->gotStageThree(std::move(stage), (stageThreeSent ? 2 : 0));
}

void Node::adjustStageThreeStorage() {
  stageThreeSent = false;
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

  sendDefault(conveyer.confidantByIndex(respondent), msgType, cs::Conveyer::instance().currentRoundNumber() , subRound_,  myConfidantIndex_, required/*, iteration*/);
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
  //uint8_t iteration;
  //istream_ >> iteration;

  if (!istream_.good() || !istream_.end()) {
    cserror() << "Bad StageThree packet format";
    return;
  }

  const cs::Conveyer& conveyer = cs::Conveyer::instance();

  if (!conveyer.isConfidantExists(requesterNumber) ||
      requester != conveyer.confidantByIndex(requesterNumber)) {
    return;
  }
  
  if (!conveyer.isConfidantExists(requiredNumber)) {
    return;
  }

  //if (iteration > conveyer.confidantsCount() / 2U) {
  //  return;
  //}

  switch (msgType) {
  case MsgTypes::FirstStageRequest:
    solver_->gotStageOneRequest(requesterNumber, requiredNumber);
    break;
  case MsgTypes::SecondStageRequest:
    solver_->gotStageTwoRequest(requesterNumber, requiredNumber);
    break;
  case MsgTypes::ThirdStageRequest:
    solver_->gotStageThreeRequest(requesterNumber, requiredNumber/*, iteration*/);
    break;
  default:
    break;
  }
}

void Node::sendStageReply(const uint8_t sender, const cs::Signature& signature, const MsgTypes msgType, const uint8_t requester, cs::Bytes& message) {
  //if (myConfidantIndex_ == 1) {
  //  return;
  //}
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

  const int k1 = (corruptionLevel_ / 8) % 2;
  const cs::Byte k2 = static_cast<cs::Byte>(corruptionLevel_ / 16);

  if (k1 == 1 && k2 == myConfidantIndex_) {
    csdebug() << "STAGE REPLY ##############> NOTHING WILL BE SENT";
   }
  else {
    sendDefault(conveyer.confidantByIndex(requester), msgType, cs::Conveyer::instance().currentRoundNumber(), subRound_, signature, message);
  }

  csmeta(csdetails) << "done";
}

void Node::sendSmartReject(const std::vector<std::pair<cs::Sequence, uint32_t>>& referenceList) {
  if (referenceList.empty()) {
    csmeta(cserror) << "cannot send empty rejected contracts pack";
    return;
  }

  cs::Bytes data;
  cs::DataStream stream(data);

  stream << referenceList;

  csdebug() << "Node: sending " << referenceList.size() << " rejected contract(s) to related smart confidants";
  sendBroadcast(MsgTypes::RejectedContracts, cs::Conveyer::instance().currentRoundNumber(), data);
}

void Node::getSmartReject(const uint8_t* data, const size_t size, const cs::RoundNumber rNum, const cs::PublicKey& sender) {
  csunused(rNum);
  csunused(sender);

  istream_.init(data, size);

  cs::Bytes bytes;
  istream_ >> bytes;

  cs::DataStream stream(bytes.data(), bytes.size());

  std::vector<std::pair<cs::Sequence, uint32_t>> referenceList;
  stream >> referenceList;

  if (referenceList.empty()) {
    csmeta(cserror) << "empty rejected contracts pack received";
    return;
  }

  csdebug() << "Node: " << referenceList.size() << " rejected contract(s) received";
  emit gotRejectedContracts(referenceList);
}

void Node::sendSmartStageOne(const cs::ConfidantsKeys& smartConfidants, cs::StageOneSmarts& stageOneInfo) {
  csmeta(csdebug) << "started";
  if (std::find(smartConfidants.cbegin(), smartConfidants.cend(), solver_->getPublicKey()) == smartConfidants.cend()) {
    cswarning() << "NODE> Only confidant nodes can send smart-contract consensus stages";
    return;
  }

  csmeta(csdetails) << std::endl
                    << "Smart starting Round: " << cs::SmartConsensus::blockPart(stageOneInfo.id)
                    << '.' << cs::SmartConsensus::transactionPart(stageOneInfo.id) << std::endl
                    << "Sender: " << static_cast<int>(stageOneInfo.sender) << std::endl
                    << "Hash: " << cs::Utils::byteStreamToHex(stageOneInfo.hash.data(), stageOneInfo.hash.size());

  size_t expectedMessageSize = sizeof(stageOneInfo.sender) + stageOneInfo.hash.size();

  cs::Bytes message;
  message.reserve(expectedMessageSize);
  cs::DataStream stream(message);
  stream << stageOneInfo.sender;
  stream << stageOneInfo.hash;

  cs::Bytes messageToSign;
  messageToSign.reserve(sizeof(cs::Hash));
  // hash of message
  stageOneInfo.messageHash = cscrypto::calculateHash(message.data(), message.size());
  cs::DataStream signStream(messageToSign);
  signStream << stageOneInfo.messageHash;

  csdebug() << "MsgHash: " << cs::Utils::byteStreamToHex(stageOneInfo.messageHash.data(), stageOneInfo.messageHash.size());

  // signature of round number + calculated hash
  stageOneInfo.signature = cscrypto::generateSignature(solver_->getPrivateKey(), messageToSign.data(), messageToSign.size());

  sendToList(smartConfidants, stageOneInfo.sender, MsgTypes::FirstSmartStage, cs::Conveyer::instance().currentRoundNumber(),
    // payload
    stageOneInfo.id, stageOneInfo.signature, message, stageOneInfo.fee.integral(), stageOneInfo.fee.fraction());

  // cache
  stageOneInfo.message = std::move(message);
  csmeta(csdebug) << "done";
}

void Node::getSmartStageOne(const uint8_t* data, const size_t size, const cs::RoundNumber, const cs::PublicKey& sender) {
  //csmeta(csdetails) << "started";
  csdebug() << __func__ << ": starting";
  //csdetails() << "Get Smart Stage One Message(recover): " << cs::Utils::byteStreamToHex(data, size);

  istream_.init(data, size);

  cs::StageOneSmarts stage;
  istream_ >> stage.id >> stage.signature;

  int32_t fee_integral = 0;
  uint64_t fee_fraction = 0;
  cs::Bytes bytes;
  istream_ >> bytes;
  istream_ >> fee_integral;
  istream_ >> fee_fraction;
  if (!istream_.good() || !istream_.end()) {
    cserror() << "Bad Smart Stage One packet format";
    return;
  }
  // hash of part received message
  stage.messageHash = cscrypto::calculateHash(bytes.data(), bytes.size());
  //csdebug() << "MsgHash: " << cs::Utils::byteStreamToHex(stage.messageHash.data(), stage.messageHash.size());

  cs::Bytes signedMessage;
  cs::DataStream signedStream(signedMessage);
  signedStream << stage.messageHash;


  // stream for main message
  cs::DataStream stream(bytes.data(), bytes.size());
  stream >> stage.sender;
  stream >> stage.hash;

  cs::Sequence block = cs::SmartConsensus::blockPart(stage.id);
  uint32_t transaction = cs::SmartConsensus::transactionPart(stage.id);
  csdebug() << __func__ << ": starting {" << block << '.' << transaction << '}';

  csdb::Amount fee{fee_integral,fee_fraction,csdb::Amount::AMOUNT_MAX_FRACTION};
  csdebug() << "Fee constructed: " << fee.to_string();
  stage.fee = fee;
  csdebug() << "StageHash: " << cs::Utils::byteStreamToHex(stage.hash.data(), stage.hash.size());
  if (!cscrypto::verifySignature(stage.signature, sender, signedMessage.data(), signedMessage.size())) {
    cswarning() << "NODE> Smart stage One from T[" << static_cast<int>(stage.sender) << "] {" 
      << block << '.' << transaction << "} -  WRONG SIGNATURE!!!";
    return;
  }

  stage.message = std::move(bytes);

  csmeta(csdebug) << "Sender: " << static_cast<int>(stage.sender)
                << ", sender key: " << cs::Utils::byteStreamToHex(sender.data(), sender.size()) << std::endl 
                << "Smart#: {" << block << '.' << transaction << '}';
  csdebug() << "Hash: " << cs::Utils::byteStreamToHex(stage.hash.data(), stage.hash.size());

  csdebug() << "NODE> SmartStage One from T[" << static_cast<int>(stage.sender) << "] is OK!";
  //solver_->gotSmartStageOne(stage);
  if (std::find(activeSmartConsensuses_.cbegin(), activeSmartConsensuses_.cend(), stage.id) == activeSmartConsensuses_.cend()) {
    csdebug() << "The SmartConsensus {" << block << '.' << transaction
      << "} is not active now, storing the stage";
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
  stream << stageTwoInfo.signatures;
  stream << stageTwoInfo.hashes;

  // create signature
  stageTwoInfo.signature = cscrypto::generateSignature(solver_->getPrivateKey(), bytes.data(), bytes.size());
  sendToList(smartConfidants, stageTwoInfo.sender,
             MsgTypes::SecondSmartStage, cs::Conveyer::instance().currentRoundNumber(),
             stageTwoInfo.id, stageTwoInfo.signature, bytes);

  // cash our stage two
  stageTwoInfo.message = std::move(bytes);
  csmeta(csdebug) << "done";
}

void Node::getSmartStageTwo(const uint8_t* data, const size_t size, const cs::RoundNumber, const cs::PublicKey& sender) {
  csmeta(csdebug);

  csdebug() << "NODE> Getting SmartStage Two from " << cs::Utils::byteStreamToHex(sender.data(), sender.size());

  istream_.init(data, size);

  cs::StageTwoSmarts stage;
  istream_ >> stage.id >> stage.signature;

  cs::Bytes bytes;
  istream_ >> bytes;

  if (!istream_.good() || !istream_.end()) {
    cserror() << "NODE> Bad SmartStageTwo packet format";
    return;
  }

  cs::DataStream stream(bytes.data(), bytes.size());
  stream >> stage.sender;
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
  stream << stageThreeInfo.realTrustedMask;
  stream << stageThreeInfo.packageSignature;

  stageThreeInfo.signature = cscrypto::generateSignature(solver_->getPrivateKey(), bytes.data(), bytes.size());
  sendToList(smartConfidants, stageThreeInfo.sender, MsgTypes::ThirdSmartStage, cs::Conveyer::instance().currentRoundNumber(),
    // payload:
             stageThreeInfo.id, stageThreeInfo.signature, bytes);
  
  // cach stage three
  stageThreeInfo.message = std::move(bytes);
  csmeta(csdebug) << "done";
}

void Node::getSmartStageThree(const uint8_t* data, const size_t size, const cs::RoundNumber, const cs::PublicKey& sender) {
  csmeta(csdetails) << "started";
  csunused(sender);

  istream_.init(data, size);

  cs::StageThreeSmarts stage;
  istream_ >> stage.id >> stage.signature;

  cs::Bytes bytes;
  istream_ >> bytes;

  if (!istream_.good() || !istream_.end()) {
    cserror() << "NODE> Bad SmartStage Three packet format";
    return;
  }

  cs::DataStream stream(bytes.data(), bytes.size());
  stream >> stage.sender;
  stream >> stage.writer;
  stream >> stage.realTrustedMask;
  stream >> stage.packageSignature;

  if (!cscrypto::verifySignature(stage.signature, sender, bytes.data(), bytes.size())) {
    csdebug() << "SmartStage Three from T[" << static_cast<int>(stage.sender) << "] -  WRONG SIGNATURE!!!";
    return;
  }

  stage.message = std::move(bytes);

  csdebug() << "NODE> SmartStage-3 from T[" << static_cast<int>(stage.sender) << "] is OK!";
  //solver_->gotSmartStageThree(stage);
  emit gotSmartStageThree(stage, false);
}

void Node::smartStageRequest(MsgTypes msgType, cs::Sequence smartRound, uint32_t startTransaction, cs::PublicKey confidant, uint8_t respondent, uint8_t required) {

  sendDefault(confidant, msgType, cs::Conveyer::instance().currentRoundNumber(), smartRound, startTransaction, respondent, required);
  csmeta(csdetails) << "done";
}

void Node::getSmartStageRequest(const MsgTypes msgType, const uint8_t* data, const size_t size, const cs::PublicKey& requester) {
csmeta(csdetails) << "started";

  istream_.init(data, size);

  uint8_t requesterNumber = 0;
  cs::Sequence smartRound;
  uint32_t startTransaction;
  istream_ >> smartRound >> startTransaction >> requesterNumber;

  uint8_t requiredNumber = 0;
  istream_ >> requiredNumber;

  if (!istream_.good() || !istream_.end()) {
    cserror() << "Bad SmartStage request packet format";
    return;
  }

  cs::PublicKey req = requester;

  emit receivedSmartStageRequest(msgType, smartRound, startTransaction, requesterNumber, requiredNumber, req);
  //solver_->gotSmartStageRequest(msgType, requesterNumber, requiredNumber);
}

void Node::sendSmartStageReply(const cs::Bytes& message, const cs::RoundNumber smartRNum, const cs::Signature& signature, const MsgTypes msgType, const cs::PublicKey& requester) {
  csmeta(csdetails) << "started";

  sendDefault(requester, msgType, cs::Conveyer::instance().currentRoundNumber(), smartRNum, signature, message);
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
    return;
  }
  activeSmartConsensuses_.erase(it);
}

void Node::checkForSavedSmartStages(uint64_t id) {
  for (auto& it : smartStageOneStorage_) {
    if (it.id == id) {
      emit gotSmartStageOne(it, false);
    }
  }
}

//TODO: this function is a part of round table building <===
void Node::addRoundSignature(const cs::StageThree& st3) {
  size_t pos =0;
  for (size_t i = 0; i < st3.realTrustedMask.size(); i++) {
    if (i == static_cast<size_t>(st3.sender)) {
      break;
    }
    if (st3.realTrustedMask[i] != cs::ConfidantConsts::InvalidConfidantIndex) {
      ++pos;
    }
  }
  csdebug() << "NODE> pos = " << pos 
    << ", poolSigsSize = "<< lastSentSignatures_.poolSignatures.size()
    << ", rtSigsSize = " << lastSentSignatures_.roundSignatures.size()
    << ", roundSigsSize = " << lastSentSignatures_.trustedConfirmation.size();
  std::copy(st3.blockSignature.cbegin(), st3.blockSignature.cend(), lastSentSignatures_.poolSignatures[pos].begin());
  std::copy(st3.roundSignature.cbegin(), st3.roundSignature.cend() ,lastSentSignatures_.roundSignatures[pos].begin());
  std::copy(st3.trustedSignature.cbegin(), st3.trustedSignature.cend(), lastSentSignatures_.trustedConfirmation[pos].begin());

  csdebug() << "NODE> Adding signatures of stage3 from T(" << cs::numeric_cast<int>(st3.sender)
            << ") = " << lastSentSignatures_.roundSignatures.size();
}

void Node::sendRoundPackage(const cs::PublicKey& target) {
  csmeta(csdetails) << "Send round table";
  sendDefault(target, MsgTypes::RoundTable, cs::Conveyer::instance().currentRoundNumber(), subRound_, lastRoundTableMessage_, lastSignaturesMessage_);

  if (!lastSentRoundData_.table.characteristic.mask.empty()) {
    csmeta(csdebug) << "Packing " << lastSentRoundData_.table.characteristic.mask.size() << " bytes of char. mask to send";
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
  stream << lastSentSignatures_.trustedConfirmation;

  csdebug() << "NODE> Send Signatures amount = " << lastSentSignatures_.roundSignatures.size();

  sendBroadcast(MsgTypes::RoundTable, cs::Conveyer::instance().currentRoundNumber(), subRound_, lastRoundTableMessage_, lastSignaturesMessage_);

  if (!lastSentRoundData_.table.characteristic.mask.empty()) {
    csmeta(csdebug) << "Packing " << lastSentRoundData_.table.characteristic.mask.size() << " bytes of char. mask to send";
  }

  /////////////////////////////////////////////////////////////////////////// screen output
  csdebug() << "------------------------------------------  SendRoundTable  ---------------------------------------";

  cs::Conveyer& conveyer = cs::Conveyer::instance();
  auto& table = conveyer.currentRoundTable();

  csdebug() << "Round " << conveyer.currentRoundNumber() << ", Confidants count " << table.confidants.size();
  csdebug() << "Hashes count: " << table.hashes.size();

  transport_->clearTasks();
  onRoundStart(table);

  // writer sometimes could not have all hashes, need check
  reviewConveyerHashes();
}

void Node::sendRoundTable() {
  becomeWriter();

  cs::Conveyer& conveyer = cs::Conveyer::instance();
  csdebug() << "SendRoundTable: add confirmation for round " << conveyer.currentRoundTable().round << " trusted";
  confirmationList_.add(lastSentRoundData_.table.round, false, conveyer.confidants(),
    cs::NodeUtils::getTrustedMask(getBlockChain().getLastBlock()), lastSentSignatures_.trustedConfirmation);

  conveyer.setRound(lastSentRoundData_.table.round);

  subRound_ = 0;

  cs::RoundTable table;
  table.round = conveyer.currentRoundNumber();
  table.confidants = lastSentRoundData_.table.confidants;
  table.hashes = lastSentRoundData_.table.hashes;

  conveyer.setTable(table);
  sendRoundPackageToAll();
}
//TODO: this function is a part of round table building <===
void Node::storeRoundPackageData(const cs::RoundTable& newRoundTable, const cs::PoolMetaInfo& poolMetaInfo,
                                 const cs::Characteristic& characteristic, cs::StageThree& st3) {
  lastSentRoundData_.table.round = newRoundTable.round;
  lastSentRoundData_.subRound = subRound_;
  // no general stored!
  lastSentRoundData_.table.confidants.clear();
  lastSentRoundData_.table.confidants = newRoundTable.confidants;

  lastSentRoundData_.table.hashes.clear();
  lastSentRoundData_.table.hashes = newRoundTable.hashes;

  lastSentRoundData_.table.characteristic.mask.clear();
  lastSentRoundData_.table.characteristic.mask = characteristic.mask;

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
  stream << lastSentRoundData_.table.confidants;
  stream << poolMetaInfo.realTrustedMask;
  stream << lastSentRoundData_.table.hashes;
  stream << poolMetaInfo.timestamp;
  stream << lastSentRoundData_.table.characteristic.mask;
  stream << poolMetaInfo.sequenceNumber;
  stream << poolMetaInfo.previousHash;
  //stream << lastSentRoundData_.poolMetaInfo.writerKey; -- we don't need to send this
  cs::Bytes trustedList;
  cs::DataStream tStream(trustedList);
  tStream << newRoundTable.round;
  tStream << newRoundTable.confidants;

  st3.trustedHash = cscrypto::calculateHash(trustedList.data(), trustedList.size());
  st3.roundHash = cscrypto::calculateHash(lastRoundTableMessage_.data(), lastRoundTableMessage_.size());
  //cs::DataStream signStream(messageToSign);
  //signStream << roundNumber_;
  //signStream << subRound_;
  //signStream << st3.roundHash;
  st3.trustedSignature = cscrypto::generateSignature(solver_->getPrivateKey(), st3.trustedHash.data(), st3.trustedHash.size());
  csdebug() << "Round = " << newRoundTable.round << ", Trusted Signature = " << cs::Utils::byteStreamToHex(st3.trustedSignature);
  st3.roundSignature = cscrypto::generateSignature(solver_->getPrivateKey(), st3.roundHash.data(), st3.roundHash.size());

  //here should be placed parcing of round table

  size_t sigSize = cs::NodeUtils::realTrustedValue(poolMetaInfo.realTrustedMask);
  csdebug() << "NODE> PoolSignatures reserved to size = " << sigSize;
  lastSentSignatures_.poolSignatures.clear();
  lastSentSignatures_.poolSignatures.resize(sigSize);
  lastSentSignatures_.roundSignatures.clear();
  lastSentSignatures_.roundSignatures.resize(sigSize);
  lastSentSignatures_.trustedConfirmation.clear();
  lastSentSignatures_.trustedConfirmation.resize(sigSize);
  csdebug() << "NODE> poolSigsSize = " << lastSentSignatures_.poolSignatures.size()
    << ", rtSigsSize = " << lastSentSignatures_.roundSignatures.size()
    << ", roundSigsSize = " << lastSentSignatures_.trustedConfirmation.size();
}

void Node::prepareRoundTable(cs::RoundTable& roundTable, const cs::PoolMetaInfo& poolMetaInfo, cs::StageThree& st3) {
  cs::Conveyer& conveyer = cs::Conveyer::instance();
  const cs::Characteristic* block_characteristic = conveyer.characteristic(conveyer.currentRoundNumber());

  if (!block_characteristic) {
    csmeta(cserror) << "Send round info characteristic not found, logic error";
    return;
  }

  storeRoundPackageData(roundTable, poolMetaInfo, *block_characteristic, st3);
  csdebug() << "NODE> StageThree prepared:";
  st3.print();
}
bool Node::receivingSignatures(const cs::Bytes& sigBytes, const cs::Bytes& roundBytes, const cs::RoundNumber rNum
    , const cs::Bytes& trustedMask, const cs::ConfidantsKeys& newConfidants
    , cs::Signatures& poolSignatures) {

  cs::Conveyer& conveyer = cs::Conveyer::instance();
  cs::ConfidantsKeys currentConfidants = conveyer.confidants();

  cs::DataStream stream(sigBytes.data(), sigBytes.size());

  stream >> poolSignatures;
  
  cs::Signatures roundSignatures;
  stream >> roundSignatures;

  cs::Signatures trustedConfirmation;
  stream >> trustedConfirmation;

  csdebug() << "NODE> PoolSigs Amnt = " << poolSignatures.size() 
            << ", TrustedSigs Amnt = " << trustedConfirmation.size()
            << ", RoundSigs Amnt = " << roundSignatures.size();

  if (trustedMask.size() != currentConfidants.size()) {
    csmeta(cserror) << "Illegal trusted mask count in round table";
    return false;
  }
  cs::Hash tempHash = cscrypto::calculateHash(roundBytes.data(), roundBytes.size());

  if (!cs::NodeUtils::checkGroupSignature(currentConfidants, trustedMask, roundSignatures, tempHash)) {
    csdebug() << "NODE> The roundtable signatures are NOT OK";
    return false;
  }
  else {
    csdebug() << "NODE> The roundtable signatures are ok";
  }

  cs::Bytes trustedToHash;
  cs::DataStream tth(trustedToHash);
  tth << rNum;
  tth << newConfidants;
  cs::Hash trustedHash = cscrypto::calculateHash(trustedToHash.data(), trustedToHash.size());
  
  if (cs::NodeUtils::checkGroupSignature(currentConfidants, trustedMask, trustedConfirmation, trustedHash)) {
    csdebug() << "NODE> The trusted confirmation for the next round are ok";
    confirmationList_.add(rNum, false, currentConfidants, trustedMask, trustedConfirmation);
  }
  else {
    csdebug() << "NODE> The trusted confirmation for the next round are NOT OK";
    //return false;
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
  cs::ConfidantsKeys prevConfidants = conveyer.confidants();
  if (conveyer.currentRoundNumber() == rNum && subRound_ > subRound) {
    cswarning() << "NODE> round table SUBROUND is lesser then local one, ignore round table";
    csmeta(csdetails) << "My subRound: " << static_cast<int>(subRound_)
                      << ", Received subRound: " << static_cast<int>(subRound);
    return;
  }

  conveyer.setRound(rNum);
  poolSynchronizer_->sync(conveyer.currentRoundNumber());

  // update sub round
  subRound_ = subRound;

  cs::Bytes roundBytes;
  istream_ >> roundBytes;

  cs::Bytes bytes;
  istream_ >> bytes;

  cs::DataStream roundStream(roundBytes.data(), roundBytes.size());
  cs::ConfidantsKeys confidants;
  roundStream >> confidants;

  if (confidants.empty()) {
    csmeta(cserror) << "Illegal confidants count in round table";
    return;
  }

  cs::Bytes realTrusted;
  roundStream >> realTrusted;

  cs::Signatures poolSignatures;

  if (!receivingSignatures(bytes, roundBytes, rNum, realTrusted, confidants, poolSignatures)){
    //return;
  }

  cs::PacketsHashes hashes;
  roundStream >> hashes;

  cs::RoundTable roundTable;
  roundTable.round = rNum;
  roundTable.confidants = std::move(confidants);
  roundTable.hashes = std::move(hashes);
  roundTable.general = sender;
  csdebug() << "NODE> confidants: " << roundTable.confidants.size();

  // first change conveyer state
  conveyer.setTable(roundTable);

  // create pool by previous round, then change conveyer state.
  getCharacteristic(reinterpret_cast<cs::Byte*>(roundStream.data()), roundStream.size()
    , conveyer.previousRoundNumber(), sender, std::move(poolSignatures), std::move(realTrusted));

  onRoundStart(conveyer.currentRoundTable());
  reviewConveyerHashes();

  csmeta(csdetails) << "done\n";
}

void Node::sendHash(cs::RoundNumber round) {
  if (!canBeTrusted()) {
    return;
  }

  if (blockChain_.getLastSequence() != round - 1) {
    // should not send hash until have got proper block sequence
    return;
  }

  csdebug() << "NODE> Sending hash to ALL";
  csdb::PoolHash spoiledHash = spoileHash(blockChain_.getLastHash(), solver_->getPublicKey());
  sendToConfidants(MsgTypes::BlockHash, round, subRound_, spoiledHash);
  csdebug() << "NODE> Hash sent, round: " << round << "." << cs::numeric_cast<int>(subRound_);
}

void Node::getHash(const uint8_t* data, const size_t size, cs::RoundNumber rNum, const cs::PublicKey& sender) {
  if (myLevel_ != Level::Confidant) {
    csdebug() << "NODE> ignore hash as no confidant";
    return;
  }

  //TODO: here shoud be placed the DPOS check 
  csdetails() << "NODE> get hash of round " << rNum << ", data size " << size;

  istream_.init(data, size);
  uint8_t subRound;
  istream_ >> subRound;

  if (subRound > subRound_) {
    cswarning() << "NODE> We got hash for the Node with SUBROUND: " << static_cast<int>(subRound) << ", we don't have";
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

  sendRoundPackage(target);

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
  stageThreeSent = false;

  constexpr int padWidth = 30;

  badHashReplyCounter_.clear();
  badHashReplyCounter_.resize(roundTable.confidants.size());

  for(auto badHash : badHashReplyCounter_) {
    badHash = false;
  }

  std::ostringstream line1;
  for (int i = 0; i < padWidth; i++) {
    line1 << '=';
  }

  line1 << " R-" << cs::Conveyer::instance().currentRoundNumber() << "." << cs::numeric_cast<int>(subRound_) << " ";

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
  cslog() << " Last written sequence = " << blockChain_.getLastSequence() << ", neighbours = " << transport_->getNeighboursCount();

  std::ostringstream line2;

  for (std::size_t i = 0; i < fixedWidth; ++i) {
    line2 << '-';
  }

  csdebug() << line2.str();
  csdebug() << " Confidants:";

  for (size_t i = 0; i < roundTable.confidants.size(); ++i) {
    auto result = myLevel_ == Level::Confidant && i == myConfidantIndex_;
    auto name =  result ? "me" : cs::Utils::byteStreamToHex(roundTable.confidants[i]);

    csdebug() << "[" << i << "] " << name;
  }

  csdebug() << " Hashes: " << roundTable.hashes.size();

  for (size_t j = 0; j < roundTable.hashes.size(); ++j) {
    csdetails() << "[" << j << "] " << cs::Utils::byteStreamToHex(roundTable.hashes[j].toBinary());
  }

  csdebug() << line2.str();
  stat_.onRoundStart(cs::Conveyer::instance().currentRoundNumber());
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

  uint8_t senderNumber;
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

  const auto badHashReplySummary = std::count_if(badHashReplyCounter_.begin(), badHashReplyCounter_.end(),
                                                 [](bool badHash) { return badHash; });

  if (static_cast<size_t>(badHashReplySummary) > conveyer.confidantsCount() / 2) {
    csmeta(csdebug) << "This node really have not valid HASH!!! Removing last block from DB and trying to syncronize";
    //TODO: examine what will be done without this function
    blockChain_.removeLastBlock();
  }
}

/*static*/
void Node::requestStop() {
  stopRequested_ = true;
}
