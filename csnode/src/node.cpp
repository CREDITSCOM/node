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

#include <poolsynchronizer.hpp>

const unsigned MIN_CONFIDANTS = 3;
const unsigned MAX_CONFIDANTS = 100;

const csdb::Address Node::genesisAddress_ = csdb::Address::from_string("0000000000000000000000000000000000000000000000000000000000000001");
const csdb::Address Node::startAddress_ = csdb::Address::from_string("0000000000000000000000000000000000000000000000000000000000000002");

Node::Node(const Config& config)
: nodeIdKey_(config.getMyPublicKey())
, blockChain_(config.getPathToDB().c_str(), genesisAddress_, startAddress_)
, solver_(new cs::SolverCore(this, genesisAddress_, startAddress_))
,
#ifdef MONITOR_NODE
    stats_(blockChain_)
,
#endif
#ifdef NODE_API
  api_(blockChain_, solver_)
,
#endif
  allocator_(1 << 24, 5)
, packStreamAllocator_(1 << 26, 5)
, ostream_(&packStreamAllocator_, nodeIdKey_) {
  transport_ = new Transport(config, this);
  poolSynchronizer_ = new cs::PoolSynchronizer(config.getPoolSyncSettings(), transport_, &blockChain_);
  good_ = init();
}

Node::~Node() {
  sendingTimer_.stop();

  delete solver_;
  delete transport_;
  delete poolSynchronizer_;
}

bool Node::init() {
  if (!cscrypto::CryptoInit()) {
    return false;
  }

  if (!transport_->isGood()) {
    return false;
  }

  if (!blockChain_.isGood()) {
    return false;
  }

  if (!solver_) {
    return false;
  }

  csdebug() << "Everything init";

  // check file with keys
  if (!checkKeysFile()) {
    return false;
  }

#ifdef SPAMMER
  runSpammer();
#endif

  cs::Connector::connect(&sendingTimer_.timeOut, this, &Node::processTimer);
  cs::Connector::connect(&cs::Conveyer::instance().flushSignal(), this, &Node::onTransactionsPacketFlushed);
  cs::Connector::connect(&poolSynchronizer_->sendRequest, this, &Node::sendBlockRequest);

  return true;
}

bool Node::checkKeysFile() {
  std::ifstream pub(publicKeyFileName_);
  std::ifstream priv(privateKeyFileName_);

  if (!pub.is_open() || !priv.is_open()) {
    cslog() << "\n\nNo suitable keys were found. Type \"g\" to generate or \"q\" to quit.";

    char gen_flag = 'a';
    std::cin >> gen_flag;

    if (gen_flag == 'g') {
      auto [generatedPublicKey, generatedPrivateKey] = generateKeys();
      solver_->setKeysPair(generatedPublicKey, generatedPrivateKey);
      return true;
    }
    else {
      return false;
    }
  }
  else {
    std::string pub58, priv58;
    std::getline(pub, pub58);
    std::getline(priv, priv58);

    pub.close();
    priv.close();

    cs::Bytes privateKey;
    cs::Bytes publicKey;

    DecodeBase58(pub58, publicKey);
    DecodeBase58(priv58, privateKey);

    if (publicKey.size() != PUBLIC_KEY_LENGTH || privateKey.size() != PRIVATE_KEY_LENGTH) {
      cslog() << "\n\nThe size of keys found is not correct. Type \"g\" to generate or \"q\" to quit.";

      char gen_flag = 'a';
      std::cin >> gen_flag;

      bool needGenerateKeys = gen_flag == 'g';

      if (gen_flag == 'g') {
        auto [generatedPublicKey, generatedPrivateKey] = generateKeys();
        solver_->setKeysPair(generatedPublicKey, generatedPrivateKey);
      }

      return needGenerateKeys;
    }

    cs::PublicKey fixedPublicKey;
    cs::PrivateKey fixedPrivatekey;

    std::copy(publicKey.begin(), publicKey.end(), fixedPublicKey.begin());
    std::copy(privateKey.begin(), privateKey.end(), fixedPrivatekey.begin());

    return checkKeysForSignature(fixedPublicKey, fixedPrivatekey);
  }
}

std::pair<cs::PublicKey, cs::PrivateKey> Node::generateKeys() {
  cs::PublicKey fixedPublicKey;
  cs::PrivateKey fixedPrivateKey;
  cscrypto::GenerateKeyPair(fixedPublicKey, fixedPrivateKey);

  std::ofstream f_pub(publicKeyFileName_);
  f_pub << EncodeBase58(cs::Bytes(fixedPublicKey.begin(), fixedPublicKey.end()));
  f_pub.close();

  std::ofstream f_priv(privateKeyFileName_);
  f_priv << EncodeBase58(cs::Bytes(fixedPrivateKey.begin(), fixedPrivateKey.end()));
  f_priv.close();

  return std::make_pair<cs::PublicKey, cs::PrivateKey>(std::move(fixedPublicKey), std::move(fixedPrivateKey));
}

bool Node::checkKeysForSignature(const cs::PublicKey& publicKey, const cs::PrivateKey& privateKey) {
  if (cscrypto::ValidateKeyPair(publicKey, privateKey)) {
    solver_->setKeysPair(publicKey, privateKey);
    return true;
  }

  cslog() << "\n\nThe keys for node are not correct. Type \"g\" to generate or \"q\" to quit.";

  char gen_flag = 'a';
  std::cin >> gen_flag;

  if (gen_flag == 'g') {
    auto [generatedPublickey, generatedPrivateKey] = generateKeys();
    solver_->setKeysPair(generatedPublickey, generatedPrivateKey);
    return true;
  }

  return false;
}

void Node::blockchainSync() {
  poolSynchronizer_->processingSync(roundNumber_);
}

void Node::run() {
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

namespace {
#ifdef MONITOR_NODE
  bool monitorNode = true;
#else
  bool monitorNode = false;
#endif
}

void Node::getBigBang(const uint8_t* data, const size_t size, const cs::RoundNumber rNum, uint8_t type) {
  csunused(type);
  cswarning() << "NODE> get BigBang #" << rNum << ": last written #" << getBlockChain().getLastWrittenSequence()
              << ", current #" << roundNumber_;

  istream_.init(data, size);

  cs::Hash last_block_hash;
  istream_ >> last_block_hash;

  cs::RoundTable global_table;
  global_table.round = rNum;

  if (!readRoundData(global_table)) {
    cserror() << "NODE> read round data from SS failed, continue without round table";
  }

  const auto& local_table = cs::Conveyer::instance().currentRoundTable();

  // currently in global round
  if (global_table.round == local_table.round) {
    // resend all this round data available
    cslog() << "NODE> resend last block hash after BigBang";
    // update round table
    onRoundStart(global_table);

    // do almost the same as reviewConveyerHashes(), only difference is call to
    // conveyer.updateRoundTable()
    cs::Conveyer& conveyer = cs::Conveyer::instance();
    conveyer.updateRoundTable(std::move(global_table));
    const auto& updated_table = conveyer.currentRoundTable();
    if (updated_table.hashes.empty() || conveyer.isSyncCompleted()) {
      startConsensus();
    }
    else {
      sendPacketHashesRequest(conveyer.currentNeededHashes(), conveyer.currentRoundNumber(), startPacketRequestPoint_);
    }

    return;
  }

  // global round is other then local one
  handleRoundMismatch(global_table);
}

void Node::getRoundTableSS(const uint8_t* data, const size_t size, const cs::RoundNumber rNum, uint8_t type) {
  csunused(type);
  istream_.init(data, size);

  cslog() << "NODE> get SS Round Table #" << rNum;
  cs::RoundTable roundTable;

  if (!readRoundData(roundTable)) {
    cserror() << "NODE> read round data from SS failed, continue without round table";
  }

  roundTable.round = rNum;

  // "normal" start
  if (roundTable.round == 1) {
    cs::Timer::singleShot(TIME_TO_AWAIT_SS_ROUND, [this, roundTable]() mutable {
      if (roundTable.round != 1) {
        return;
      }
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
void Node::handleRoundMismatch(const cs::RoundTable& global_table) {
  const auto& local_table = cs::Conveyer::instance().currentRoundTable();
  if (local_table.round == global_table.round) {
    // mismatch not confirmed
    return;
  }

  // global round is behind local one
  if (local_table.round > global_table.round) {
    // TODO: in case of bigbang, rollback round(s), then accept global_table, then start round again

    if (local_table.round - global_table.round == 1) {
      cslog() << "NODE> re-send last round info may help others to go to round #" << local_table.round;
      tryResendRoundTable(std::nullopt, local_table.round);  // broadcast round info
    }
    else {
      // TODO: Test if we are in proper blockchain

      // TODO: rollback local round to global one

      cserror() << "NODE> round rollback (from #" << local_table.round << " to #" << global_table.round
                << " not implemented yet";
    }
    return;
  }

  // local round is behind global one
  const auto last_block = getBlockChain().getLastWrittenSequence();
  if (last_block + cs::Conveyer::HashTablesStorageCapacity < global_table.round) {
    // activate pool synchronizer
    poolSynchronizer_->processingSync(global_table.round);
    // no return, ask for next round info
  }

  // broadcast request round info
  cswarning() << "NODE> broadcast request round info";
  sendNextRoundRequest();

  //// directly request from trusted
  return;
}

uint32_t Node::getRoundNumber() {
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

  cs::Timer::singleShot(TIME_TO_AWAIT_ACTIVITY << 5, [this] {
    stop();
  });
}

void Node::getPacketHashesRequest(const uint8_t* data, const std::size_t size, const cs::RoundNumber round, const cs::PublicKey& sender) {
  istream_.init(data, size);

  std::size_t hashesCount = 0;
  istream_ >> hashesCount;

  csdebug() << "NODE> Get packet hashes request: sender " << cs::Utils::byteStreamToHex(sender.data(), sender.size());

  cs::PacketsHashes hashes;
  hashes.reserve(hashesCount);

  for (std::size_t i = 0; i < hashesCount; ++i) {
    cs::TransactionsPacketHash hash;
    istream_ >> hash;

    hashes.push_back(std::move(hash));
  }

  cslog() << "NODE> Requested packet hashes: " << hashesCount;

  if (hashesCount != hashes.size()) {
    cserror() << "NODE> wrong hashes list requested";
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

  std::size_t packetsCount = 0;
  istream_ >> packetsCount;

  cs::Packets packets;
  packets.reserve(packetsCount);

  for (std::size_t i = 0; i < packetsCount; ++i) {
    cs::TransactionsPacket packet;
    istream_ >> packet;

    if (!packet.transactions().empty()) {
      packets.push_back(std::move(packet));
    }
  }

  if (packets.size() != packetsCount) {
    cserror() << "NODE> Packet hashes reply, bad packets parsing";
    return;
  }

  csdebug() << "NODE> Get packet hashes reply: sender " << cs::Utils::byteStreamToHex(sender.data(), sender.size());
  cslog() << "NODE> Hashes reply got packets count: " << packetsCount;

  processPacketsReply(std::move(packets), round);
}

void Node::getCharacteristic(const uint8_t* data, const size_t size, const cs::RoundNumber round, const cs::PublicKey& sender) {
  csprint();
  cs::Conveyer& conveyer = cs::Conveyer::instance();

  if (!conveyer.isSyncCompleted(round)) {
    cslog() << "\tpacket sync not finished, saving characteristic meta to call after sync";

    cs::Bytes characteristicBytes(data, data + size);

    cs::CharacteristicMeta meta;
    meta.bytes = std::move(characteristicBytes);
    meta.sender = sender;

    conveyer.addCharacteristicMeta(round, std::move(meta));
    return;
  }

  istream_.init(data, size);

  std::string time;
  cs::Bytes characteristicMask;
  csdb::Pool::sequence_t sequence = 0;

  cslog() << "\tconveyer sync completed, parsing data size " << size;

  istream_ >> time;
  istream_ >> characteristicMask >> sequence;

  cs::PoolMetaInfo poolMetaInfo;
  poolMetaInfo.sequenceNumber = sequence;
  poolMetaInfo.timestamp = std::move(time);

  cs::Signature signature;
  istream_ >> signature;

  cs::PublicKey writerPublicKey;
  istream_ >> writerPublicKey;

  if (!istream_.good()) {
    cserror() << "NODE> " << __func__ << "(): round info parsing failed, data is corrupted";
    return;
  }

  cslog() << "\tsequence " << poolMetaInfo.sequenceNumber << ", mask size " << characteristicMask.size();
  csdebug() << "\ttime = " << poolMetaInfo.timestamp;

  if (getBlockChain().getLastWrittenSequence() < sequence) {
    // otherwise senseless, this block is already in chain
    cs::Characteristic characteristic;
    characteristic.mask = std::move(characteristicMask);

    stat_.totalReceivedTransactions_ += characteristic.mask.size();

    assert(sequence <= this->getRoundNumber());

    ////////////////////////////////////////////////////////////////////////////////////////////////////
    conveyer.setCharacteristic(characteristic, poolMetaInfo.sequenceNumber);
    cs::PublicKey pk;
    std::fill(pk.begin(), pk.end(), 0);
    std::optional<csdb::Pool> pool = conveyer.applyCharacteristic(poolMetaInfo, pk);

    if (!pool.has_value()) {
      cserror() << "NODE> " << __func__ << "(): created pool is not valid";
      return;
    }

    const auto ptable = conveyer.roundTable(round);
    if (ptable == nullptr) {
      cserror() << "NODE> cannot access proper round table to add trusted to pool #" << poolMetaInfo.sequenceNumber;
    }
    else {
      std::vector<cs::Bytes> confs;
      for (const auto& src : ptable->confidants) {
        auto& tmp = confs.emplace_back(cs::Bytes(src.size()));
        std::copy(src.cbegin(), src.cend(), tmp.begin());
      }
      pool.value().set_confidants(confs);
    }

    if (!getBlockChain().storeBlock(pool.value(), false /*by_sync*/)) {
      cserror() << "NODE> failed to store block in BlockChain";
    }
    else {
      stat_.totalAcceptedTransactions_ += pool.value().transactions_count();
      getBlockChain().testCachedBlocks();
    }
  }

  csprint() << "done";
}

const cs::ConfidantsKeys& Node::confidants() const {
  return cs::Conveyer::instance().currentRoundTable().confidants;
}

void Node::createRoundPackage(const cs::RoundTable& roundTable, const cs::PoolMetaInfo& poolMetaInfo,
                              const cs::Characteristic& characteristic, const cs::Signature& signature) {
  ostream_.init(BaseFlags::Broadcast | BaseFlags::Compressed | BaseFlags::Fragmented);
  ostream_ << MsgTypes::RoundTable << roundNumber_;
  ostream_ << roundTable.confidants.size();
  ostream_ << roundTable.hashes.size();

  for (const auto& confidant : roundTable.confidants) {
    ostream_ << confidant;
  }

  for (const auto& hash : roundTable.hashes) {
    ostream_ << hash;
  }

  ostream_ << poolMetaInfo.timestamp;

  if (!characteristic.mask.empty()) {
    cslog() << "NODE> packing " << characteristic.mask.size() << " bytes of char. mask to send";
  }

  ostream_ << characteristic.mask;
  ostream_ << poolMetaInfo.sequenceNumber;
  ostream_ << signature;
  ostream_ << solver_->getPublicKey();
}

void Node::storeRoundPackageData(const cs::RoundTable& roundTable, const cs::PoolMetaInfo& poolMetaInfo,
                                 const cs::Characteristic& characteristic, const cs::Signature& signature) {
  lastSentRoundData_.roundTable.round = roundTable.round;

  // no general stored!
  lastSentRoundData_.roundTable.confidants.resize(roundTable.confidants.size());
  std::copy(roundTable.confidants.cbegin(), roundTable.confidants.cend(),
            lastSentRoundData_.roundTable.confidants.begin());
  lastSentRoundData_.roundTable.hashes.resize(roundTable.hashes.size());
  std::copy(roundTable.hashes.cbegin(), roundTable.hashes.cend(), lastSentRoundData_.roundTable.hashes.begin());

  lastSentRoundData_.characteristic.mask.resize(characteristic.mask.size());
  std::copy(characteristic.mask.cbegin(), characteristic.mask.cend(), lastSentRoundData_.characteristic.mask.begin());
  lastSentRoundData_.poolMetaInfo.sequenceNumber = poolMetaInfo.sequenceNumber;
  lastSentRoundData_.poolMetaInfo.timestamp = poolMetaInfo.timestamp;
  lastSentRoundData_.poolSignature = signature;
}

void Node::sendTransactionsPacket(const cs::TransactionsPacket& packet) {
  if (packet.hash().isEmpty()) {
    cswarning() << "Send transaction packet with empty hash failed";
    return;
  }

  sendBroadcast(MsgTypes::TransactionPacket, roundNumber_, packet);
}

void Node::sendPacketHashesRequest(const cs::PacketsHashes& hashes, const cs::RoundNumber round, uint32_t requestStep) {
  if (cs::Conveyer::instance().isSyncCompleted(round)) {
    return;
  }

  csdebug() << "NODE> Sending packet hashes request: " << hashes.size();

  cs::PublicKey main;
  const auto msgType = MsgTypes::TransactionsPacketRequest;
  const auto roundTable = cs::Conveyer::instance().roundTable(round);

  // look at main node
  main = (roundTable != nullptr) ? roundTable->general : cs::Conveyer::instance().currentRoundTable().general;

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
  cs::Timer::singleShot(static_cast<int>(cs::NeighboursRequestDelay + requestStep), requestClosure);
}

void Node::sendPacketHashesRequestToRandomNeighbour(const cs::PacketsHashes& hashes, const cs::RoundNumber round) {
  const auto msgType = MsgTypes::TransactionsPacketRequest;
  const auto neighboursCount = transport_->getNeighboursCount();

  bool successRequest = false;

  for (std::size_t i = 0; i < neighboursCount; ++i) {
    ConnectionPtr connection = transport_->getNeighbourByNumber(i);

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

void Node::resetNeighbours() {
  transport_->resetNeighbours();
}

void Node::getBlockRequest(const uint8_t* data, const size_t size, const cs::PublicKey& sender) {
  cslog() << "NODE> Get Block Request";

  std::size_t sequencesCount = 0;

  istream_.init(data, size);
  istream_ >> sequencesCount;

  csdebug() << "NODE> Block request got sequences count: " << sequencesCount;
  csdebug() << "NODE> Get packet hashes request: sender " << cs::Utils::byteStreamToHex(sender.data(), sender.size());

  if (sequencesCount == 0) {
    return;
  }

  cs::PoolsRequestedSequences sequences;
  sequences.reserve(sequencesCount);

  for (std::size_t i = 0; i < sequencesCount; ++i) {
    cs::RoundNumber sequence;
    istream_ >> sequence;

    sequences.push_back(std::move(sequence));
  }

  uint32_t packetNum = 0;
  istream_ >> packetNum;

  cslog() << "NODE> Get block request> Getting the request for block: from: " << sequences.front() << ", to: " << sequences.back() << ",  id: " << packetNum;

  if (sequencesCount != sequences.size()) {
    cserror() << "Bad sequences created";
    return;
  }

  if (sequences.front() > blockChain_.getLastWrittenSequence()) {
    cslog() << "NODE> Get block request> The requested block: " << sequences.front() << " is BEYOND my CHAIN";
    return;
  }

  cs::PoolsBlock poolsBlock;
  poolsBlock.reserve(sequencesCount);

  for (auto& sequence : sequences) {
    csdb::Pool pool = blockChain_.loadBlock(blockChain_.getHashBySequence(sequence));

    if (pool.is_valid()) {
      auto prev_hash = csdb::PoolHash::from_string("");
      pool.set_previous_hash(prev_hash);

      poolsBlock.push_back(std::move(pool));

      sendBlockReply(poolsBlock, sender, packetNum);
      poolsBlock.clear();
    }
  }
}

void Node::getBlockReply(const uint8_t* data, const size_t size) {
  if (!poolSynchronizer_->isSyncroStarted()) {
    csdebug() << "NODE> Get block reply> Pool synchronizer already syncro";
    return;
  }

  cslog() << "NODE> Get Block Reply";

  std::size_t poolsCount = 0;

  istream_.init(data, size);
  istream_ >> poolsCount;

  if (!poolsCount) {
    cserror() << "NODE> Get block reply> Pools count is 0";
    return;
  }

  cs::PoolsBlock poolsBlock;
  poolsBlock.reserve(poolsCount);

  for (std::size_t i = 0; i < poolsCount; ++i) {
    csdb::Pool pool;
    istream_ >> pool;

    transport_->syncReplied(cs::numeric_cast<uint32_t>(pool.sequence()));
    poolsBlock.push_back(std::move(pool));
  }

  uint32_t packetNum = 0;
  istream_ >> packetNum;

  poolSynchronizer_->getBlockReply(std::move(poolsBlock), packetNum);
}

void Node::sendBlockReply(const cs::PoolsBlock& poolsBlock, const cs::PublicKey& target, uint32_t packetNum) {
  for (const auto& pool : poolsBlock) {
    csdebug() << "NODE> Send block reply. Sequence: " << pool.sequence();
  }

  tryToSendDirect(target, MsgTypes::RequestedBlock, cs::Conveyer::instance().currentRoundNumber(), poolsBlock, packetNum);
}

void Node::becomeWriter() {
  myLevel_ = NodeLevel::Writer;
  cslog() << "NODE> Became writer";
}

void Node::processPacketsRequest(cs::PacketsHashes&& hashes, const cs::RoundNumber round, const cs::PublicKey& sender) {
  csdebug() << "NODE> Processing packets sync request";

  cs::Packets packets;

  const auto& conveyer = cs::Conveyer::instance();
  cs::SharedLock lock(conveyer.sharedMutex());

  for (const auto& hash : hashes) {
    std::optional<cs::TransactionsPacket> packet = conveyer.findPacket(hash, round);

    if (packet) {
      packets.push_back(std::move(packet).value());
    }
  }

  if (packets.size()) {
    csdebug() << "NODE> Found packets in storage: " << packets.size();
    sendPacketHashesReply(packets, round, sender);
  }
  else {
    csdebug() << "NODE> Cannot find packets in storage";
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
    resetNeighbours();

    if (auto meta = conveyer.characteristicMeta(round); meta.has_value()) {
      csdebug() << "NODE> Run characteristic meta";
      getCharacteristic(meta->bytes.data(), meta->bytes.size(), round, meta->sender);

      // if next block maybe stored, the last written sequence maybe updated, so deferred consensus maybe resumed
      if(getBlockChain().getLastWrittenSequence() + 1 == getRoundNumber()) {
        cslog() << "NODE> got all blocks written in current round";
        startConsensus();
      }
    }
  }
}

void Node::processTransactionsPacket(cs::TransactionsPacket&& packet) {
  cs::Conveyer::instance().addTransactionsPacket(packet);
}

void Node::reviewConveyerHashes() {
  cs::Conveyer& conveyer = cs::Conveyer::instance();
  //conveyer.setRound(std::move(roundTable));
  const auto& table = conveyer.currentRoundTable();

  if (table.hashes.empty() || conveyer.isSyncCompleted()) {
    if (table.hashes.empty()) {
      cslog() << "NODE> No hashes in round table, start consensus now";
    }
    else {
      cslog() << "NODE> All hashes in conveyer, start consensus now";
    }

    startConsensus();
  }
  else {
    sendPacketHashesRequest(conveyer.currentNeededHashes(), conveyer.currentRoundNumber(), startPacketRequestPoint_);
  }
}

bool Node::isPoolsSyncroStarted() {
  return poolSynchronizer_->isSyncroStarted();
}

uint8_t Node::getConfidantNumber() {
  return myConfidantIndex_;
}

void Node::processTimer() {
  const auto round = cs::Conveyer::instance().currentRoundNumber();

  if (myLevel_ != NodeLevel::Normal || round <= cs::TransactionsFlushRound) {
    return;
  }

  cs::Conveyer::instance().flushTransactions();
}

void Node::onTransactionsPacketFlushed(const cs::TransactionsPacket& packet) {
  CallsQueue::instance().insert(std::bind(&Node::sendTransactionsPacket, this, packet));
}

void Node::sendBlockRequest(const ConnectionPtr target, const cs::PoolsRequestedSequences sequences, uint32_t packetNum) {
  const auto round = cs::Conveyer::instance().currentRoundNumber();
  csdetails() << "NODE> " << __func__ << "() Target out(): " << target->getOut()
              << ", sequence from: " << sequences.front() << ", to: " << sequences.back() << ", packet: " << packetNum
              << ", round: " << round;

  transport_->deliverDirect(ostream_.getPackets(), ostream_.getPacketsCount(), target);

  ostream_.clear();
}

Node::MessageActions Node::chooseMessageAction(const cs::RoundNumber rNum, const MsgTypes type) {
  if (!good_) {
    return MessageActions::Drop;
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
      if (cs::Conveyer::instance().currentRoundNumber() > 1) {
        // not on the very start
        cswarning() << "NODE> detect round lag, request round info";
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
    return (rNum <= round ? MessageActions::Process : MessageActions::Drop);
  }

  if (type == MsgTypes::RoundTableReply) {
    return (rNum >= round ? MessageActions::Process : MessageActions::Drop);
  }

  if (type == MsgTypes::BlockHash) {
    if (rNum < round) {
      // outdated
      return MessageActions::Drop;
    }

    if (rNum > getBlockChain().getLastWrittenSequence() + cs::Conveyer::HashTablesStorageCapacity) {
      // too many rounds behind the global round
      return MessageActions::Drop;
    }

    if (rNum > round) {
      cslog() << "NODE> outrunning block hash (#" << rNum << ") is postponed until get round info";
      return MessageActions::Postpone;
    }

    if (!cs::Conveyer::instance().isSyncCompleted()) {
      cslog() << "NODE> block hash is postponed until conveyer sync is completed";
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

  cslog() << "NODE> Number of confidants :" << cs::numeric_cast<int>(confSize);

  if (confSize < MIN_CONFIDANTS || confSize > MAX_CONFIDANTS) {
    cswarning() << "Bad confidants num";
    return false;
  }

  cs::ConfidantsKeys confidants;
  confidants.reserve(confSize);

  istream_ >> mainNode;

  while (istream_) {
    cs::PublicKey key;
    istream_ >> key;

    confidants.push_back(key);

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

  csdetails() << "NODE> Sending Direct data: packets count = " << ostream_.getPacketsCount() << ", last size = " << (ostream_.getCurrentSize())
              << ", out = " << target->out
              << ", in = " << target->in
              << ", specialOut = " << target->specialOut
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
    const auto& confidant = confidants[i];

    if (myConfidantIndex_ == i && nodeIdKey_ == confidant) {
      continue;
    }

    sendBroadcast(confidant, msgType, round, std::forward<Args>(args)...);
  }
}

template <typename... Args>
void Node::writeDefaultStream(Args&&... args) {
  (ostream_ << ... << std::forward<Args>(args));  // fold expression
}

template<typename... Args>
bool Node::sendToNeighbours(const MsgTypes msgType, const cs::RoundNumber round, Args&&... args) {
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

  csdetails() << "NODE> Sending broadcast data: size = " << ostream_.getCurrentSize() << ", round: " << round
              << ", msgType: " << getMsgTypesString(msgType);

  transport_->deliverBroadcast(ostream_.getPackets(), ostream_.getPacketsCount());
  ostream_.clear();
}

void Node::sendStageOne(cs::StageOne& stageOneInfo) {
  if (myLevel_ != NodeLevel::Confidant) {
    cswarning() << "NODE> Only confidant nodes can send consensus stages";
    return;
  }

  stageOneInfo.roundTimeStamp = cs::Utils::currentTimestamp();
  
  csprint() << "(): Round = " << roundNumber_ << ", Sender: " << static_cast<int>(stageOneInfo.sender)
    << ", Cand Amount: " << stageOneInfo.trustedCandidates.size()
    << ", Hashes Amount: " << stageOneInfo.hashesCandidates.size()
    << ", Time Stamp: " << stageOneInfo.roundTimeStamp << std::endl
    << "Hash: " << cs::Utils::byteStreamToHex(stageOneInfo.hash.data(), stageOneInfo.hash.size());

  size_t expectedMessageSize = sizeof(stageOneInfo.sender)
                  + sizeof(stageOneInfo.hash)
                  + sizeof(size_t)
                  + sizeof(cs::Hash) * stageOneInfo.trustedCandidates.size()
                  + sizeof(stageOneInfo.hashesCandidates.size())
                  + sizeof(cs::Hash) * stageOneInfo.hashesCandidates.size()
                  + sizeof(size_t)
                  + stageOneInfo.roundTimeStamp.size();

  cs::Bytes message;
  message.reserve(expectedMessageSize);

  cs::Bytes messageToSign;
  messageToSign.reserve(sizeof(cs::RoundNumber) + sizeof(cs::Hash));

  cs::DataStream stream(message);
  stream << stageOneInfo.sender;
  stream << stageOneInfo.hash;
  stream << stageOneInfo.trustedCandidates;
  stream << stageOneInfo.hashesCandidates;
  stream << stageOneInfo.roundTimeStamp;

  // hash of message
  cscrypto::CalculateHash(stageOneInfo.messageHash, message.data(), message.size());

  cs::DataStream signStream(messageToSign);
  signStream << roundNumber_ << stageOneInfo.messageHash;

  cslog() << "MsgHash: " << cs::Utils::byteStreamToHex(stageOneInfo.messageHash.data(), stageOneInfo.messageHash.size());

  // signature of round number + calculated hash
  cscrypto::GenerateSignature(stageOneInfo.signature, solver_->getPrivateKey(), messageToSign.data(), messageToSign.size());

  sendToConfidants(MsgTypes::FirstStage, roundNumber_, stageOneInfo.signature, message);

  // cache
  stageOneMessage_[myConfidantIndex_] = std::move(message);
  csprint() << "(): done";
}

void Node::getStageOne(const uint8_t* data, const size_t size, const cs::PublicKey& sender) {
  csprint() << "started";

  if (myLevel_ != NodeLevel::Confidant) {
    csdebug() << "NODE> ignore stage-1 as no confidant";
    return;
  }

  csunused(sender);

  istream_.init(data, size);

  cs::StageOne stage;
  cs::Bytes bytes;
  istream_ >> stage.signature >> bytes;

  if (!istream_.good() || !istream_.end()) {
    cserror() << "Bad StageOne packet format";
    return;
  }

  // hash of part received message
  cscrypto::CalculateHash(stage.messageHash, bytes.data(), bytes.size());

  cs::Bytes signedMessage;
  cs::DataStream signedStream(signedMessage);
  signedStream << roundNumber_ << stage.messageHash;

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
    cswarning() << "NODE> Stage One from [" << static_cast<int>(stage.sender) << "] -  WRONG SIGNATURE!!!";
    return;
  }

  stageOneMessage_[stage.sender] = std::move(bytes);

  cslog() << __func__ <<  "(): Sender: " << static_cast<int>(stage.sender) << ", sender key: "
          << cs::Utils::byteStreamToHex(confidant.data(), confidant.size())
          << " - " << cs::Utils::byteStreamToHex(sender.data(), sender.size());
  cslog() << "Message hash: " << cs::Utils::byteStreamToHex(stage.messageHash.data(), stage.messageHash.size());

  csdebug() << "NODE> Stage One from [" << static_cast<int>(stage.sender) << "] is OK!";
  solver_->gotStageOne(std::move(stage));
}

void Node::sendStageTwo(cs::StageTwo& stageTwoInfo) {
  csprint() << "started";

  if ((myLevel_ != NodeLevel::Confidant) && (myLevel_ != NodeLevel::Writer)) {
    cswarning() << "Only confidant nodes can send consensus stages";
    return;
  }

  size_t confidantsCount = cs::Conveyer::instance().confidantsCount();
  size_t stageBytesSize  = sizeof(stageTwoInfo.sender) + (sizeof(cs::Signature) + sizeof(cs::Hash)) * confidantsCount;

  cs::Bytes bytes;
  bytes.reserve(stageBytesSize);

  cs::DataStream stream(bytes);
  stream << stageTwoInfo.sender;
  stream << stageTwoInfo.signatures;
  stream << stageTwoInfo.hashes;

  // create signature
  cscrypto::GenerateSignature(stageTwoInfo.signature, solver_->getPrivateKey(), bytes.data(), bytes.size());

  sendToConfidants(MsgTypes::SecondStage, roundNumber_, bytes, stageTwoInfo.signature);

  // cash our stage two
  stageTwoMessage_[myConfidantIndex_] = std::move(bytes);
  csprint() << "done";
}

void Node::getStageTwo(const uint8_t* data, const size_t size, const cs::PublicKey& sender) {
  csprint();

  if ((myLevel_ != NodeLevel::Confidant) && (myLevel_ != NodeLevel::Writer)) {
    csdebug() << "NODE> ignore stage two as no confidant";
    return;
  }

  cslog() << "Getting Stage Two from " << cs::Utils::byteStreamToHex(sender.data(), sender.size());
  istream_.init(data, size);

  cs::Bytes bytes;
  istream_ >> bytes;

  cs::StageTwo stage;
  istream_ >> stage.signature;

  if (!istream_.good() || !istream_.end()) {
    cserror() << "Bad StageTwo packet format";
    return;
  }

  cs::DataStream stream(bytes.data(), bytes.size());
  stream >> stage.sender;
  stream >> stage.signatures;
  stream >> stage.hashes;

  cslog() << __func__  << "(): Sender :" << cs::Utils::byteStreamToHex(sender.data(), sender.size());
  const cs::Conveyer& conveyer = cs::Conveyer::instance();

  if (!conveyer.isConfidantExists(stage.sender)) {
    return;
  }

  if (!cscrypto::VerifySignature(stage.signature, conveyer.confidantByIndex(stage.sender), bytes.data(), bytes.size())) {
    cslog() << "Stage Two from [" << static_cast<int>(stage.sender) << "] -  WRONG SIGNATURE!!!";
    return;
  }

  csprint() << "Signature is OK";
  stageTwoMessage_[stage.sender] = std::move(bytes);

  csdebug() << "NODE> Stage Two from [" << static_cast<int>(stage.sender) << "] is OK!";
  solver_->gotStageTwo(std::move(stage));
}

void Node::sendStageThree(cs::StageThree& stageThreeInfo) {
  csprint() << "started";

  if (myLevel_ != NodeLevel::Confidant) {
    cswarning() << "NODE> Only confidant nodes can send consensus stages";
    return;
  }

  // TODO: think how to improve this code
  stageThreeMessage_.clear();

  size_t stageSize = 2 * sizeof(uint8_t) + 3 * sizeof(cs::Hash) + stageThreeInfo.realTrustedMask.size();

  cs::Bytes bytes;
  bytes.reserve(stageSize);

  cs::DataStream stream(bytes);
  stream << stageThreeInfo.sender;
  stream << stageThreeInfo.writer;
  stream << stageThreeInfo.hashBlock;
  stream << stageThreeInfo.hashHashesList;
  stream << stageThreeInfo.hashCandidatesList;
  stream << stageThreeInfo.realTrustedMask;

  cscrypto::GenerateSignature(stageThreeInfo.signature,solver_->getPrivateKey(), bytes.data(), bytes.size());

  sendToConfidants(MsgTypes::ThirdStage, roundNumber_, bytes, stageThreeInfo.signature);

  // cach stage three
  stageThreeMessage_[myConfidantIndex_] = std::move(bytes);
  csprint() << "done";
}

void Node::getStageThree(const uint8_t* data, const size_t size, const cs::PublicKey& sender) {
  csdetails() << "NODE> " << __func__ << "()";
  csunused(sender);

  if (myLevel_ != NodeLevel::Confidant && myLevel_ != NodeLevel::Writer) {
    csdebug() << "NODE> ignore stage-3 as no confidant";
    return;
  }

  istream_.init(data, size);

  cs::Bytes bytes;
  istream_ >> bytes;

  cs::StageThree stage;
  istream_  >> stage.signature;

  if (!istream_.good() || !istream_.end()) {
    cserror() << "NODE> Bad StageTwo packet format";
    return;
  }

  cs::DataStream stream(bytes.data(), bytes.size());
  stream >> stage.sender;
  stream >> stage.writer;
  stream >> stage.hashBlock;
  stream >> stage.hashHashesList;
  stream >> stage.hashCandidatesList;
  stream >> stage.realTrustedMask;

  const cs::Conveyer& conveyer = cs::Conveyer::instance();

  if (!conveyer.isConfidantExists(stage.sender)) {
    return;
  }

  if (!cscrypto::VerifySignature(stage.signature, conveyer.confidantByIndex(stage.sender), bytes.data(), bytes.size())) {
    cswarning() << "NODE> Stage Three from [" << static_cast<int>(stage.sender) << "] -  WRONG SIGNATURE!!!";
    return;
  }

  stageThreeMessage_[stage.sender] = std::move(bytes);

  csdebug() << "NODE> Stage Three from [" << static_cast<int>(stage.sender) << "] is OK!";
  solver_->gotStageThree(std::move(stage));
}

void Node::stageRequest(MsgTypes msgType, uint8_t respondent, uint8_t required) {
  if ((myLevel_ != NodeLevel::Confidant) && (myLevel_ != NodeLevel::Writer)) {
    cswarning() << "NODE> Only confidant nodes can request consensus stages";
    return;
  }

  const cs::Conveyer& conveyer = cs::Conveyer::instance();

  if (!conveyer.isConfidantExists(respondent)) {
    return;
  }

  sendDefault(conveyer.confidantByIndex(respondent), msgType, roundNumber_ , myConfidantIndex_, required);
  csprint() << "done";
}

void Node::getStageRequest(const MsgTypes msgType, const uint8_t* data, const size_t size, const cs::PublicKey& requester) {
  csprint() << "started";

  if (myLevel_ != NodeLevel::Confidant) {
    return;
  }

  istream_.init(data, size);

  uint8_t requesterNumber = 0;
  uint8_t requiredNumber = 0;
  istream_ >> requesterNumber >> requiredNumber;

  const cs::Conveyer& conveyer = cs::Conveyer::instance();

  if (!conveyer.isConfidantExists(requesterNumber)) {
    return;
  }

  if (requester != conveyer.confidantByIndex(requesterNumber)) {
    return;
  }

  if (!istream_.good() || !istream_.end()) {
    cserror() << "Bad StageThree packet format";
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
  }
}

void Node::sendStageReply(const uint8_t sender, const cscrypto::Signature& signature, const MsgTypes msgType, const uint8_t requester) {
  csprint() << "started";

  if (myLevel_ != NodeLevel::Confidant) {
    cswarning() << "NODE> Only confidant nodes can send consensus stages";
    return;
  }

  const cs::Conveyer& conveyer = cs::Conveyer::instance();

  if (!conveyer.isConfidantExists(requester)) {
    return;
  }

  const cs::PublicKey& confidant = conveyer.confidantByIndex(requester);
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
  }

  sendDefault(confidant, msgType, roundNumber_, signature, message);
  csprint() << "done";
}

void Node::prepareMetaForSending(cs::RoundTable& roundTable, std::string timeStamp) {
  csprint() << " timestamp = " << timeStamp;

  // only for new consensus
  cs::PoolMetaInfo poolMetaInfo;
  poolMetaInfo.sequenceNumber = blockChain_.getLastWrittenSequence() + 1;  // change for roundNumber
  poolMetaInfo.timestamp = timeStamp;

  /////////////////////////////////////////////////////////////////////////// preparing block meta info
  cs::Conveyer& conveyer = cs::Conveyer::instance();
  cs::PublicKey pk;
  std::fill(pk.begin(), pk.end(), 0);
  std::optional<csdb::Pool> pool = conveyer.applyCharacteristic(poolMetaInfo, pk);
  if (!pool.has_value()) {
    cserror() << "NODE> applyCharacteristic() failed to create block";
    return;
  }

  std::vector<std::vector<uint8_t>> confs;

  for(const auto& src : roundTable.confidants) {
    auto& tmp = confs.emplace_back(std::vector<uint8_t>(src.size()));
    std::copy(src.cbegin(), src.cend(), tmp.begin());
  }

  pool.value().set_confidants(confs);
  pool = getBlockChain().createBlock(pool.value());

  if(!pool.has_value()) {
    cserror() << "NODE> blockchain failed to write new block";
    return;
  }

  stat_.totalAcceptedTransactions_ += pool.value().transactions_count();

  // array
  cs::Signature poolSignature;
  const auto& signature = pool.value().signature();
  std::copy(signature.begin(), signature.end(), poolSignature.begin());

  //logPool(pool.value());
  sendRoundTable(roundTable, poolMetaInfo, poolSignature);
}

void Node::sendRoundTable(cs::RoundTable& roundTable, cs::PoolMetaInfo poolMetaInfo, const cs::Signature& poolSignature) {
  cs::Conveyer& conveyer = cs::Conveyer::instance();
  roundNumber_ = roundTable.round;

  const cs::Characteristic* block_characteristic = conveyer.characteristic(conveyer.currentRoundNumber());

  if (!block_characteristic) {
    cserror() << "Send round info characteristic not found, logic error";
    return;
  }
  stat_.totalReceivedTransactions_ += block_characteristic->mask.size();

  conveyer.setRound(std::move(roundTable));
  /////////////////////////////////////////////////////////////////////////// sending round info and block
  createRoundPackage(conveyer.currentRoundTable(), poolMetaInfo, *block_characteristic, poolSignature);
  storeRoundPackageData(conveyer.currentRoundTable(), poolMetaInfo, *block_characteristic, poolSignature);

  flushCurrentTasks();

  /////////////////////////////////////////////////////////////////////////// screen output
  cslog() << "------------------------------------------  SendRoundTable  ---------------------------------------";
  const cs::RoundTable& table = conveyer.currentRoundTable();
  const cs::ConfidantsKeys confidants = table.confidants;
  cslog() << "Round " << roundNumber_ << ", Confidants count " << confidants.size();

  const cs::PacketsHashes& hashes = table.hashes;
  cslog() << "Hashes count: " << hashes.size();

  transport_->clearTasks();

  onRoundStart(table);
  startConsensus();
}

void Node::getRoundTable(const uint8_t* data, const size_t size, const cs::RoundNumber rNum, const cs::PublicKey& sender) {
  csdebug() << "\n";
  csprint();

  if (myLevel_ == NodeLevel::Writer) {
    cswarning() << "\tWriters don't need ROUNDINFO";
    return;
  }

  istream_.init(data, size);

  // RoundTable evocation
  std::size_t confidantsCount = 0;
  istream_ >> confidantsCount;

  if (confidantsCount == 0) {
    csprint() << "Bad confidants count in round table";
    return;
  }

  std::size_t hashesCount = 0;
  istream_ >> hashesCount;

  cs::RoundTable roundTable;
  roundTable.round = rNum;

  // to node
  cs::ConfidantsKeys confidants;
  confidants.reserve(confidantsCount);

  for (std::size_t i = 0; i < confidantsCount; ++i) {
    cs::PublicKey key;
    istream_ >> key;

    confidants.push_back(std::move(key));
  }

  cs::PacketsHashes hashes;
  hashes.reserve(hashesCount);

  for (std::size_t i = 0; i < hashesCount; ++i) {
    cs::TransactionsPacketHash hash;
    istream_ >> hash;

    hashes.push_back(hash);
  }

  roundTable.confidants = std::move(confidants);
  roundTable.hashes = std::move(hashes);
  roundTable.general = sender;
  cslog() << "\tconfidants: " << roundTable.confidants.size();

  cs::Conveyer::instance().setRound(std::move(roundTable));
  getCharacteristic(istream_.getCurrentPtr(), istream_.remainsBytes(), rNum, sender);

  onRoundStart(cs::Conveyer::instance().currentRoundTable());
  blockchainSync();
  reviewConveyerHashes();

  csprint() << "done\n";
}

void Node::sendHash(cs::RoundNumber round) {
  if (monitorNode) {
    // to block request trusted status
    return;
  }

  if (getBlockChain().getLastWrittenSequence() != round - 1) {
    // should not send hash until have got proper block sequence
    return;
  }

  const auto& hash = getBlockChain().getLastWrittenHash();
  // = personallyDamagedHash();

  cslog() << "Sending hash " << hash.to_string() << " to ALL";
  sendToConfidants(MsgTypes::BlockHash, round, hash);
}

void Node::getHash(const uint8_t* data, const size_t size, cs::RoundNumber rNum, const cs::PublicKey& sender) {
  if (myLevel_ != NodeLevel::Confidant) {
    csdebug() << "NODE> ignore hash as no confidant";
    return;
  }

  csdetails() << "NODE> get hash of round " << rNum << ", data size " << size;

  istream_.init(data, size);

  csdb::PoolHash tmp;
  istream_ >> tmp;

  if (!istream_.good() || !istream_.end()) {
    cswarning() << "NODE> bad hash packet format";
    return;
  }

  solver_->gotHash(std::move(tmp), sender);
}

void Node::sendRoundTableRequest(uint8_t respondent) {
  // ask for round info from current trusted on current round
  auto confidant = cs::Conveyer::instance().confidantIfExists(respondent);

  if (confidant) {
    sendRoundTableRequest(confidant.value());
  }
  else {
    cserror() << "NODE> cannot request round info, incorrect respondent number";
  }
}

constexpr const uint8_t InvalidTrustedIndex = uint8_t(-1);

void Node::sendNextRoundRequest() {
  // 0xFF means we ask for last writer node simply to repeat round info
  sendBroadcast(MsgTypes::RoundTableRequest, roundNumber_, InvalidTrustedIndex);
}

void Node::sendRoundTableRequest(const cs::PublicKey& respondent) {
  cslog() << "NODE> send request for next round info after #" << roundNumber_;

  // ask for next round info:
  sendDefault(respondent, MsgTypes::RoundTableRequest, roundNumber_ + 1, myConfidantIndex_);
}

void Node::getRoundTableRequest(const uint8_t* data, const size_t size, const cs::RoundNumber rNum, const cs::PublicKey& requester) {
  csprint();

  istream_.init(data, size);

  uint8_t requesterNumber;
  istream_ >> requesterNumber;

  if (!istream_.good() || !istream_.end()) {
    cserror() << "NODE> bad RoundInfo request packet format";
    return;
  }

  // special request to re-send again handling
  if (requesterNumber == InvalidTrustedIndex) {
    csdebug() << "NODE> som enode asks for last round info to repeat";

    if (lastSentRoundData_.roundTable.round == rNum) {
      if (tryResendRoundTable(requester, rNum)) {
        cslog() << "NODE> round info #" << rNum << " has sent again";
      }
      else {
        cslog() << "NODE> unable to send round info #" << rNum << " again";
      }
    }

    return;
  }

  // default request from other trusted node handling
  cslog() << "NODE> get request for next round info after #" << rNum << " from [" << (int)requesterNumber << "]";
  solver_->gotRoundInfoRequest(requester, rNum);
}

void Node::sendRoundTableReply(const cs::PublicKey& target, bool has_requested_info) {
  cslog() << "NODE> send RoundInfo reply to " << cs::Utils::byteStreamToHex(target.data(), target.size());

  if (myLevel_ != NodeLevel::Confidant) {
    cswarning() << "Only confidant nodes can reply consensus stages";
  }

  auto request = (has_requested_info ? (uint8_t)1 : (uint8_t)0);
  sendDefault(target, MsgTypes::RoundTableReply, roundNumber_, request);
}

bool Node::tryResendRoundTable(std::optional<const cs::PublicKey> respondent, cs::RoundNumber rNum) {
  csunused(respondent);

  if (lastSentRoundData_.roundTable.round != rNum) {
    cswarning() << "NODE> unable to repeat round data #" << rNum;
    return false;
  }

  //TODO: use respondent.value() to send info directly, otherwise broadcast info
  createRoundPackage(lastSentRoundData_.roundTable, lastSentRoundData_.poolMetaInfo, lastSentRoundData_.characteristic, lastSentRoundData_.poolSignature);
  flushCurrentTasks();

  cslog() << "NODE> re-send last round info #" << rNum << " to ALL";
  return true;
}

void Node::getRoundTableReply(const uint8_t* data, const size_t size, const cs::PublicKey& respondent) {
  csprint();

  if (myLevel_ != NodeLevel::Confidant) {
    return;
  }

  istream_.init(data, size);

  uint8_t reply;
  istream_ >> reply;

  if (!istream_.good() || !istream_.end()) {
    cserror() << "NODE> bad RoundInfo reply packet format";
    return;
  }

  solver_->gotRoundInfoReply(reply != 0, respondent);
}

void Node::onRoundStart(const cs::RoundTable& roundTable) {
  roundNumber_ = roundTable.round;
  bool found = false;
  uint8_t conf_no = 0;

  for (auto& conf : roundTable.confidants) {
    if (conf == nodeIdKey_) {
      myLevel_ = NodeLevel::Confidant;
      myConfidantIndex_ = conf_no;
      found = true;
      break;
    }

    conf_no++;
  }

  if (!found) {
    myLevel_ = NodeLevel::Normal;
  }

  stageOneMessage_.clear();
  stageOneMessage_.resize(roundTable.confidants.size());
  stageTwoMessage_.clear();
  stageTwoMessage_.resize(roundTable.confidants.size());
  stageThreeMessage_.clear();
  stageThreeMessage_.resize(roundTable.confidants.size());

  constexpr int pad_width = 30;
  int width = 0;

  std::ostringstream line1;
  for (int i = 0; i < pad_width; i++) {
    line1 << '=';
  }

  width += pad_width;
  line1 << " ROUND " << roundNumber_ << ". ";
  width += 9;

  if (NodeLevel::Normal == myLevel_) {
    line1 << "NORMAL";
    width += 6;
  }
  else {
    line1 << "TRUSTED [" << (int)myConfidantIndex_ << "]";
    width += 11;

    if (myConfidantIndex_ > 9) {
      width += 1;
    }
  }

  line1 << ' ';
  width += 1;

  for (int i = 0; i < pad_width; i++) {
    line1 << '=';
  }

  width += pad_width;

  const auto s = line1.str();
  int fixed_width = (int)s.size();

  cslog() << s;
  cslog() << " Node key " << cs::Utils::byteStreamToHex(nodeIdKey_.data(), nodeIdKey_.size());
  cslog() << " last written sequence = " << getBlockChain().getLastWrittenSequence();

  std::ostringstream line2;

  for (int i = 0; i < fixed_width; ++i) {
    line2 << '-';
  }

  cslog() << line2.str();

  cslog() << " Confidants:";
  int i = 0;

  for (const auto& e : roundTable.confidants) {
    cslog() << "[" << i << "] "
            << (NodeLevel::Confidant == myLevel_ && i == myConfidantIndex_
                    ? "me"
                    : cs::Utils::byteStreamToHex(e.data(), e.size()));
    ++i;
  }

  cslog() << " Hashes: " << roundTable.hashes.size();

  for (size_t j = 0; j < roundTable.hashes.size(); ++j) {
    csdetails() << "[" << j << "] " << cs::Utils::byteStreamToHex(roundTable.hashes.at(j).toBinary().data(), roundTable.hashes.at(j).size());
  }

  cslog() << line2.str();
  stat_.onRoundStart(roundNumber_);
  cslog() << line2.str();

  solver_->nextRound();

  if (!sendingTimer_.isRunning()) {
    cslog() << "NODE> Transaction timer started";
    sendingTimer_.start(cs::TransactionsPacketInterval);
  }
}

void Node::startConsensus() {
  cs::RoundNumber roundNumber = cs::Conveyer::instance().currentRoundNumber();
  solver_->gotConveyerSync(roundNumber);
  transport_->processPostponed(roundNumber);

  // claim the trusted role only if have got proper blockchain:
  if (roundNumber == getBlockChain().getLastWrittenSequence() + 1) {
    sendHash(roundNumber);
  }
}
