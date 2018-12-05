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

  if(!pub.is_open() || !priv.is_open()) {
    cslog() << "\n\nNo suitable keys were found. Type \"g\" to generate or \"q\" to quit.";

    char gen_flag = 'a';
    std::cin >> gen_flag;

    if(gen_flag == 'g') {
      auto[generatedPublicKey, generatedPrivateKey] = generateKeys();
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

    if(publicKey.size() != PUBLIC_KEY_LENGTH || privateKey.size() != PRIVATE_KEY_LENGTH) {
      cslog() << "\n\nThe size of keys found is not correct. Type \"g\" to generate or \"q\" to quit.";

      char gen_flag = 'a';
      std::cin >> gen_flag;

      bool needGenerateKeys = gen_flag == 'g';

      if(gen_flag == 'g') {
        auto[generatedPublicKey, generatedPrivateKey] = generateKeys();
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

bool Node::checkKeysForSignature(const cs::PublicKey& publicKey, const cs::PrivateKey& privateKey)
{
  if(cscrypto::ValidateKeyPair(publicKey, privateKey)) {
    solver_->setKeysPair(publicKey, privateKey);
    return true;
  }
  cslog() << "\n\nThe keys for node are not correct. Type \"g\" to generate or \"q\" to quit.";

  char gen_flag = 'a';
  std::cin >> gen_flag;

  if(gen_flag == 'g') {
    auto[generatedPublickey, generatedPrivateKey] = generateKeys();
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

namespace
{
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
    onRoundStart_V3(global_table);

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
  // TODO: what this call was intended for? transport_->clearTasks();

  // "normal" start
  if (roundTable.round == 1) {
    cs::Timer::singleShot(TIME_TO_AWAIT_SS_ROUND, [this, roundTable]() mutable {
      if (roundTable.round != 1) {
        return;
      }
      onRoundStart_V3(roundTable);
      cs::Conveyer::instance().setRound(std::move(roundTable));
      reviewConveyerHashes();
    });

    return;
  }

  // "hot" start
  handleRoundMismatch(roundTable);
}

// handle mismatch between own round & global round, calling code should detect mismatch before calling to the method
void Node::handleRoundMismatch(const cs::RoundTable& global_table)
{
  const auto& local_table = cs::Conveyer::instance().currentRoundTable();
  if(local_table.round == global_table.round) {
    // mismatch not confirmed
    return;
  }

  // global round is behind local one
  if(local_table.round > global_table.round) {

    //TODO: in case of bigbang, rollback round(s), then accept global_table, then start round again
    
    if(local_table.round - global_table.round == 1) {
      cslog() << "NODE> re-send last round info may help others to go to round #" << local_table.round;
      tryResendRoundTable(std::nullopt, local_table.round); // broadcast round info
    }
    else {

      //TODO: Test if we are in proper blockchain

      //TODO: rollback local round to global one
      
      cserror() << "NODE> round rollback (from #" << local_table.round << " to #" << global_table.round << " not implemented yet";
    }
    return;
  }

  // local round is behind global one
  const auto last_block = getBlockChain().getLastWrittenSequence();
  if(last_block + cs::Conveyer::HashTablesStorageCapacity < global_table.round) {
    // activate pool synchronizer
    poolSynchronizer_->processingSync(global_table.round);
    // no return, ask for next round info
  }
  // broadcast request round info
  cswarning() << "NODE> broadcast request round info";
  sendNextRoundRequest();
  //// directly request from trusted
  //cswarning() << "NODE> request round info from trusted nodes";
  //sendRoundInfoRequest(table.general);
  //for(const auto& trusted : table.confidants) {
  //  sendRoundInfoRequest(trusted);
  //}
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

void Node::getCharacteristic(const uint8_t* data, const size_t size, const cs::RoundNumber round, const cs::PublicKey& sender)
{
  cslog() << "NODE> " << __func__ << "():";
  cs::Conveyer& conveyer = cs::Conveyer::instance();

  if(!conveyer.isSyncCompleted(round)) {
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

  if(!istream_.good()) {
    cserror() << "NODE> " << __func__ << "(): round info parsing failed, data is corrupted";
    return;
  }

  cslog() << "\tsequence " << poolMetaInfo.sequenceNumber << ", mask size " << characteristicMask.size();
  csdebug() << "\ttime = " << poolMetaInfo.timestamp;

  if(getBlockChain().getLastWrittenSequence() < sequence) {
    // otherwise senseless, this block is already in chain
    cs::Characteristic characteristic;
    characteristic.mask = std::move(characteristicMask);

    stat_.totalReceivedTransactions_ += characteristic.mask.size();

    assert(sequence <= this->getRoundNumber());

    ////////////////////////////////////////////////////////////////////////////////////////////////////
    conveyer.setCharacteristic(characteristic, poolMetaInfo.sequenceNumber);
    cs::PublicKey pk;
    std::fill(pk.begin(), pk.end(), 0);
    std::optional<csdb::Pool> pool = conveyer.applyCharacteristic(poolMetaInfo, pk); // writerPublicKey);

    if(!pool.has_value()) {
      cserror() << "NODE> getCharacteristic(): created pool is not valid";
      return;
    }

    const auto ptable = conveyer.roundTable(round);
    if(nullptr == ptable) {
      cserror() << "NODE> cannot access proper round table to add trusted to pool #" << poolMetaInfo.sequenceNumber;
    }
    else {
      std::vector<std::vector<uint8_t>> confs;
      for(const auto& src : ptable->confidants) {
        auto& tmp = confs.emplace_back(std::vector<uint8_t>(src.size()));
        std::copy(src.cbegin(), src.cend(), tmp.begin());
      }
      pool.value().set_confidants(confs);
    }

    if(!getBlockChain().storeBlock(pool.value(), false /*by_sync*/)) {
      cserror() << "NODE> failed to store block in BlockChain";
    }
    else {
      stat_.totalAcceptedTransactions_ += pool.value().transactions_count();
      getBlockChain().testCachedBlocks();
    }
  }
  csdebug() << "NODE> " << __func__ << "(): done";
}

const cs::ConfidantsKeys& Node::confidants() const {
  return cs::Conveyer::instance().currentRoundTable().confidants;
}

void Node::createRoundPackage(const cs::RoundTable& roundTable,
  const cs::PoolMetaInfo& poolMetaInfo,
  const cs::Characteristic& characteristic,
  const cs::Signature& signature/*,
  const cs::Notifications& notifications*/) {
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
  // ostream_ << notifications;
  ostream_ << solver_->getPublicKey();
}

void Node::storeRoundPackageData(const cs::RoundTable& roundTable,
    const cs::PoolMetaInfo& poolMetaInfo,
    const cs::Characteristic& characteristic,
    const cs::Signature& signature/*,
    const cs::Notifications& notifications*/)
{
  lastSentRoundData_.roundTable.round = roundTable.round;
  // no general stored!
  lastSentRoundData_.roundTable.confidants.resize(roundTable.confidants.size());
  std::copy(roundTable.confidants.cbegin(), roundTable.confidants.cend(),
            lastSentRoundData_.roundTable.confidants.begin());
  lastSentRoundData_.roundTable.hashes.resize(roundTable.hashes.size());
  std::copy(roundTable.hashes.cbegin(), roundTable.hashes.cend(), lastSentRoundData_.roundTable.hashes.begin());
  // do no store charBytes, they are not in use while send round info
  // last_sent_round_data.round_table.charBytes.mask.resize(roundTable.charBytes.mask.size());
  // std::copy(roundTable.charBytes.mask.cbegin(), roundTable.charBytes.mask.cend(),
  // last_sent_round_data.round_table.charBytes.mask.begin());
  lastSentRoundData_.characteristic.mask.resize(characteristic.mask.size());
  std::copy(characteristic.mask.cbegin(), characteristic.mask.cend(), lastSentRoundData_.characteristic.mask.begin());
  lastSentRoundData_.poolMetaInfo.sequenceNumber = poolMetaInfo.sequenceNumber;
  lastSentRoundData_.poolMetaInfo.timestamp = poolMetaInfo.timestamp;
  lastSentRoundData_.poolSignature = signature;
  // last_sent_round_data.notifications.resize(notifications.size());
  // std::copy(notifications.cbegin(), notifications.cend(), last_sent_round_data.notifications.begin());
}

void Node::sendTransactionsPacket(const cs::TransactionsPacket& packet) {
  if (packet.hash().isEmpty()) {
    cswarning() << "Send transaction packet with empty hash failed";
    return;
  }

  ostream_.init(BaseFlags::Compressed | BaseFlags::Fragmented | BaseFlags::Broadcast);
  ostream_ << MsgTypes::TransactionPacket << roundNumber_ << packet;

  flushCurrentTasks();
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
    csdebug() << "NODE> Reply transaction packets: failed send to "
              << cs::Utils::byteStreamToHex(target.data(), target.size()) << ", perform broadcast";
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

  cslog() << "NODE> Get block request> Getting the request for block: from: " << sequences.front() << ", to: " << sequences.back() << ",  packet: " << packetNum;

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

//  sendBlockReply(poolsBlock, sender, packetNum);
}

void Node::getBlockReply(const uint8_t* data, const size_t size, const cs::PublicKey& sender) {
  cslog() << "NODE> Get Block Reply";
  csdebug() << "NODE> Get block reply> Sender: " << cs::Utils::byteStreamToHex(sender.data(), sender.size());

  if (!poolSynchronizer_->isSyncroStarted()) {
    csdebug() << "NODE> Get block reply> Pool synchronizer already is syncro started";
    return;
  }

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

  // if (cs::Conveyer::instance().isEnoughNotifications(cs::Conveyer::NotificationState::GreaterEqual)) {
  //  applyNotifications();
  //}
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
    csdebug() << "NODE> Packets sync completed, round " << round;
    resetNeighbours();
    cslog() << "NODE> processPacketsReply -> got Round";
    startConsensus();

    if (auto meta = conveyer.characteristicMeta(round); meta.has_value()) {
      csdebug() << "NODE> Run characteristic meta";
      getCharacteristic(meta->bytes.data(), meta->bytes.size(), round, meta->sender);
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
  csdebug() << "NODE> " << __func__ << "() Target out(): " << target->getOut()
            << ", sequence from: " << sequences.front() << ", to: " << sequences.back() << ", packet: " << packetNum
            << ", round: " << round;

  ostream_.init(BaseFlags::Neighbours | BaseFlags::Signed | BaseFlags::Compressed);
  ostream_ << MsgTypes::BlockRequest << round << sequences << packetNum;

  transport_->deliverDirect(ostream_.getPackets(), ostream_.getPacketsCount(), target);

  ostream_.clear();
}

Node::MessageActions Node::chooseMessageAction(const cs::RoundNumber rNum, const MsgTypes type) {
  if(!good_) {
    return MessageActions::Drop;
  }

  if(type == MsgTypes::NodeStopRequest) {
    return MessageActions::Process;
  }

  const auto round = cs::Conveyer::instance().currentRoundNumber();

  // starts next round, otherwise
  if(type == MsgTypes::RoundTable) {
    if(rNum > round) {
      return MessageActions::Process;
    }
    //TODO: detect absence of proper current round info (round may be set by SS or BB)
    return MessageActions::Drop;
  }

  // BB: every round (for now) may be handled:
  if(type == MsgTypes::BigBang) {
    return MessageActions::Process;
  }

  if(type == MsgTypes::BlockRequest || type == MsgTypes::RequestedBlock) {
    // which round would not be on the remote we may require the requested block or get block request
    return MessageActions::Process;
  }

  // outrunning packets of other types talk about round lag
  if(rNum > round) {
    if(rNum - round == 1) {
      // wait for next round
      return MessageActions::Postpone;
    }
    else {
      // more then 1 round lag, request round info
      if(cs::Conveyer::instance().currentRoundNumber() > 1) {
        // not on the very start
        cswarning() << "NODE> detect round lag, request round info";
        cs::RoundTable empty_table;
        empty_table.round = rNum;
        handleRoundMismatch(empty_table);
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

  if (type == MsgTypes::RoundTable) {
    // obsolete message
    cswarning() << "NODE> drop obsolete MsgTypes::RoundTable";
    return MessageActions::Drop;
  }

  if (type == MsgTypes::RoundTableRequest) {
    // obsolete message
    cswarning() << "NODE> drop obsolete MsgTypes::RoundTableRequest";
    return MessageActions::Drop;
  }

  if (type == MsgTypes::RoundTableRequest) {
    return (rNum <= round ? MessageActions::Process : MessageActions::Drop);
  }

  if (type == MsgTypes::RoundTableReply) {
    return (rNum >= round ? MessageActions::Process : MessageActions::Drop);
  }

  if (type == MsgTypes::BlockHashV3) {
    if (rNum < round) {
      // outdated
      return MessageActions::Drop;
    }
    if(rNum > getBlockChain().getLastWrittenSequence() + cs::Conveyer::HashTablesStorageCapacity) {
      // too many rounds behind the global round
      return MessageActions::Drop;
    }
    if(rNum > round) {
      cslog() << "NODE> outrunning block hash (#" << rNum << ") is postponed until get round info";
      return MessageActions::Postpone;
    }
    if(!cs::Conveyer::instance().isSyncCompleted()) {
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
  static constexpr cs::Byte noFlags = 0;

  ostream_.init(noFlags, target);
  csdebug() << "NODE> Sending default to key: " << cs::Utils::byteStreamToHex(target.data(), target.size());

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

  csdebug() << "NODE> Sending Direct data: packets count = " << ostream_.getPacketsCount() << ", last size = " << (ostream_.getCurrentSize())
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
  csdebug() << "NODE> Sending broadcast";

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
  for (const auto& confidant : confidants) {
    sendBroadcast(confidant, msgType, round, std::forward<Args>(args)...);
  }
}

template <typename T, typename... Args>
void Node::writeDefaultStream(const T& value, Args&&... args) {
  ostream_ << value;
  writeDefaultStream(std::forward<Args>(args)...);
}

template <typename T>
void Node::writeDefaultStream(const T& value) {
  ostream_ << value;
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
void Node::sendBroadcast(const cs::PublicKey& target, const MsgTypes& msgType, const cs::RoundNumber round,
                         Args&&... args) {
  ostream_.init(BaseFlags::Fragmented | BaseFlags::Compressed, target);
  csdebug() << "NODE> Sending broadcast to key: " << cs::Utils::byteStreamToHex(target.data(), target.size());

  sendBroadcastImpl(msgType, round, std::forward<Args>(args)...);
}

template <typename... Args>
void Node::sendBroadcastImpl(const MsgTypes& msgType, const cs::RoundNumber round, Args&&... args) {
  ostream_ << msgType << round;

  writeDefaultStream(std::forward<Args>(args)...);

  csdebug() << "NODE> Sending broadcast data: size = " << ostream_.getCurrentSize()
      << ", round: " << round
      << ", msgType: " << getMsgTypesString(msgType);

  transport_->deliverBroadcast(ostream_.getPackets(), ostream_.getPacketsCount());
  ostream_.clear();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////                                              SOLVER 3 METHODS (START)                                     ////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//      | |                                          | |                                               | |
//      \ /                                          \ /                                               \ /
//       V                                            V                                                 V

void Node::sendStageOne(cs::StageOne& stageOneInfo) {
  if (myLevel_ != NodeLevel::Confidant) {
    cswarning() << "NODE> Only confidant nodes can send consensus stages";
    return;
  }

  stageOneInfo.roundTimeStamp = cs::Utils::currentTimestamp();
  
  csdebug() << "NODE> " << __func__ << "(): Round = " << roundNumber_ << ", Sender: " << (int)stageOneInfo.sender
    << ", Cand Amount: " << (int)stageOneInfo.trustedCandidates.size()
    << ", Hashes Amount: " << (int)stageOneInfo.hashesCandidates.size()
    << ", Time Stamp: " << stageOneInfo.roundTimeStamp << std::endl
    << "Hash: " << cs::Utils::byteStreamToHex(stageOneInfo.hash.data(), stageOneInfo.hash.size());


  size_t pStageOneMsgSize = sizeof(stageOneInfo.sender) 
                  + stageOneInfo.hash.size()
                  + sizeof(uint8_t)
                  + sizeof(cscrypto::Hash) * stageOneInfo.trustedCandidates.size()
                  + sizeof(stageOneInfo.hashesCandidates.size())
                  + sizeof(cscrypto::Hash) * stageOneInfo.hashesCandidates.size()
                  + sizeof(uint8_t)
                  + stageOneInfo.roundTimeStamp.size();

  size_t hashedMsgSize = pStageOneMsgSize + sizeof(cs::RoundNumber) + sizeof(cs::Hash);
  auto memPtr = allocator_.allocateNext(hashedMsgSize);
  uint8_t* rawData = (uint8_t*)memPtr.get();
  uint8_t* ptr = rawData;

  memcpy(rawData, &roundNumber_, sizeof(cs::RoundNumber));
  ptr += sizeof(cs::RoundNumber);
  ptr += sizeof(cs::Hash);
  *ptr = stageOneInfo.sender;
  ++ptr;

  memcpy(ptr, stageOneInfo.hash.data(), stageOneInfo.hash.size());
  ptr += sizeof(cs::Hash);
  *ptr = (uint8_t)stageOneInfo.trustedCandidates.size();

  uint8_t tc = *ptr;
  ptr += sizeof(uint8_t);

  csdebug() << "Sending TRUSTED Candidates: " << (int)tc;

  for (auto& it : stageOneInfo.trustedCandidates) {
    memcpy(ptr, it.data(), sizeof(cs::PublicKey));
    ptr += sizeof(cs::PublicKey);
  }

  size_t hashesCandidatesAmount = stageOneInfo.hashesCandidates.size();
  memcpy(ptr, &hashesCandidatesAmount, sizeof(size_t));

  ptr += sizeof(size_t);

  if (hashesCandidatesAmount > 0) {
    csdebug() << "Sending HASHES Candidates: " << (int)hashesCandidatesAmount;

    for (auto& it : stageOneInfo.hashesCandidates) {
      memcpy(ptr, it.toBinary().data(), sizeof(cs::Hash));
      ptr += sizeof(cs::Hash);
    }
  }

  *ptr = (uint8_t)stageOneInfo.roundTimeStamp.size();
  ptr += sizeof(uint8_t);
  memcpy(ptr, stageOneInfo.roundTimeStamp.data(), stageOneInfo.roundTimeStamp.size());
  cscrypto::CalculateHash(stageOneInfo.msgHash, rawData + sizeof(cs::RoundNumber) + sizeof(cs::Hash), pStageOneMsgSize);
  memcpy(rawData + sizeof(cs::RoundNumber), stageOneInfo.msgHash.data(), sizeof(cs::Hash));
  cslog() << "MsgHash: " << cs::Utils::byteStreamToHex((const char*)stageOneInfo.msgHash.data(), 32);
  cscrypto::GenerateSignature(stageOneInfo.sig, solver_->getPrivateKey(), rawData, sizeof(cs::RoundNumber) + sizeof(cs::Hash));
  pStageOneMessage_[myConfidantIndex_] = std::string(cs::numeric_cast<const char*>((void*)(rawData + sizeof(cs::RoundNumber) + sizeof(cs::Hash))), pStageOneMsgSize);
  sendToConfidants(MsgTypes::FirstStage, roundNumber_, stageOneInfo.sig, pStageOneMessage_[myConfidantIndex_]);
  csdebug() << __func__ << "(): done";
}

void Node::getStageOneRequest(const uint8_t* data, const size_t size, const cs::PublicKey& requester) {
  csdebug() << "NODE> " << __func__ << "()";

  if (myLevel_ != NodeLevel::Confidant) {
    return;
  }

  istream_.init(data, size);

  uint8_t requesterNumber = 0;
  uint8_t requiredNumber = 0;
  istream_ >> requesterNumber >> requiredNumber;

  const cs::ConfidantsKeys& confidants = cs::Conveyer::instance().confidants();

  if (confidants.size() <= requesterNumber) {
    cserror() << __func__ << ", index " << int(requesterNumber) << ", confidants size " << confidants.size();
    return;
  }

  if (requester != confidants[requesterNumber]) {
    return;
  }

  if (!istream_.good() || !istream_.end()) {
    cslog() << "NODE> Bad StageOne packet format";
    return;
  }

  solver_->gotStageOneRequest(requesterNumber, requiredNumber);
}

void Node::sendStageReply(const uint8_t sender, const cscrypto::Signature sig, const MsgTypes msgType, const uint8_t requester) {
  csdebug() << "NODE> " << __func__ << "()";

  if (myLevel_ != NodeLevel::Confidant) {
    cswarning() << "NODE> Only confidant nodes can send consensus stages";
    return;
  }

  const auto& confidants = cs::Conveyer::instance().roundTable(roundNumber_)->confidants;

  if (confidants.size() <= requester) {
    cserror() << __func__ << " index out of range, " << int(requester) << ", confidants size " << confidants.size();
    return;
  }
  switch (msgType){
    case MsgTypes::FirstStage: 
      sendDefault(confidants.at(requester), MsgTypes::FirstStage, roundNumber_, sig, pStageOneMessage_[sender]);
      break;
    case MsgTypes::SecondStage:
      sendDefault(confidants.at(requester), MsgTypes::SecondStage, roundNumber_, sig, pStageTwoMessage_[sender]);
      break;
    case MsgTypes::ThirdStage:
      sendDefault(confidants.at(requester), MsgTypes::ThirdStage, roundNumber_, sig, pStageThreeMessage_[sender]);
      break;
  }
  csdebug() << "NODE> " << __func__ << "(): done";
}


void Node::getStageOne(const uint8_t* data, const size_t size, const cs::PublicKey& sender) {
  csdebug() << "NODE> " << __func__ << "()";
  if (myLevel_ != NodeLevel::Confidant) {
    csdebug() << "NODE> ignore stage-1 as no confidant";
    return;
  }

  csunused(sender);

  istream_.init(data, size);
  size_t msgSize;
  std::string rawBytes;
  cs::StageOne stage;
  istream_ >> stage.sig
    >> rawBytes;
  if (!istream_.good() || !istream_.end()) {
    cserror() << "Bad StageOne packet format";
    return;
  }
  msgSize = rawBytes.size();

  const uint8_t* stagePtr = (const uint8_t*)rawBytes.data();
  auto memPtr = allocator_.allocateNext(static_cast<uint32_t>(msgSize + sizeof(cs::RoundNumber) + sizeof(cs::Hash)));

  uint8_t* rawData = (uint8_t*)memPtr.get();
  memcpy(rawData, &roundNumber_, sizeof(cs::RoundNumber));
  memcpy(rawData + sizeof(cs::RoundNumber) + sizeof(cs::Hash), stagePtr, msgSize);

  cscrypto::CalculateHash(stage.msgHash, stagePtr, msgSize);
  memcpy(rawData + sizeof(cs::RoundNumber), stage.msgHash.data(), stage.msgHash.size());

  uint8_t* ptr = rawData + sizeof(cs::RoundNumber) + sizeof(cs::Hash);
  stage.sender = *ptr;

  //const cs::Conveyer& conveyer = cs::Conveyer::instance();

  //if (!conveyer.isConfidantExists(stage.sender)) {

  pStageOneMessage_[stage.sender] = rawBytes;

  cslog() << __func__ <<  "(): Sender: " << (int)stage.sender << ", sender key: "
    << cs::Utils::byteStreamToHex((const char*)cs::Conveyer::instance().roundTable(roundNumber_)->confidants.at(stage.sender).data(), 32) << " - " << cs::Utils::byteStreamToHex((const char*)sender.data(), 32);
  cslog() << "Message hash: " << cs::Utils::byteStreamToHex((const char*)stage.msgHash.data(),32);
  if (!cscrypto::VerifySignature(stage.sig, cs::Conveyer::instance().roundTable(roundNumber_)->confidants.at(stage.sender), 
    rawData, sizeof(cs::RoundNumber) + sizeof(cs::Hash))) {
    cswarning() << "NODE> Stage One from [" << (int)stage.sender << "] -  WRONG SIGNATURE!!!";
    return;
  }

  const cs::PublicKey& confidant = cs::Conveyer::instance().roundTable(roundNumber_)->confidants.at(stage.sender); //conveyer.confidantByIndex(stage.sender);

  cslog() << __func__ <<  "(): Sender: " << static_cast<int>(stage.sender) << ", sender key: "
    << cs::Utils::byteStreamToHex(confidant.data(), confidant.size());

  if (!cscrypto::VerifySignature(stage.sig, confidant, rawData, sizeof(cs::RoundNumber) + sizeof(cs::Hash))) {
    cswarning() << "NODE> Stage One from [" << (int) stage.sender << "] -  WRONG SIGNATURE!!!";
    return;
  }

  ptr += sizeof(uint8_t);
  memcpy(stage.hash.data(), ptr, stage.hash.size());
  ptr += sizeof(cs::Hash);

  uint8_t trustedCandAmount = *ptr;
  cs::PublicKey tempKey;
  stage.trustedCandidates.reserve(trustedCandAmount);
  ptr += sizeof(uint8_t);

  cslog() << "Trusted Candidates Amount = " << (int)trustedCandAmount;

  for (int i = 0; i < trustedCandAmount; ++i) {
    memcpy(tempKey.data(), ptr, 32);
    stage.trustedCandidates.push_back(tempKey);
    ptr += tempKey.size();
  }

  size_t hashesCandAmount = (size_t)*ptr;
  cslog() << "HashesAmount = " << hashesCandAmount;
  cs::TransactionsPacketHash tempHash;
  stage.hashesCandidates.reserve(hashesCandAmount);
  ptr += sizeof(size_t);

  for (int i = 0; i < hashesCandAmount; i++) {
    cs::Bytes byteHash(ptr, ptr + sizeof(cs::Hash));
    stage.hashesCandidates.push_back(cs::TransactionsPacketHash::fromBinary(byteHash));
    ptr += sizeof(cs::Hash);
  }

  size_t tSize = (uint8_t)*ptr;
  ptr += sizeof(uint8_t);

  std::string currentTimeStamp((const char*)ptr, tSize);
  stage.roundTimeStamp = currentTimeStamp;

  csdebug() << "NODE> Stage One from [" << (int) stage.sender << "] is OK!";
  solver_->gotStageOne(std::move(stage));
}

void Node::sendStageTwo(cs::StageTwo& stageTwoInfo) {
  csdebug() << "NODE> " << __func__ << "()";
  if ((myLevel_ != NodeLevel::Confidant) && (myLevel_ != NodeLevel::Writer)) {
    cswarning() << "Only confidant nodes can send consensus stages";
    return;
  }
  pStageTwoMessage_.clear();
  size_t curTrustedAmount = cs::Conveyer::instance().roundTable(roundNumber_)->confidants.size();
  size_t pStageTwoMsgSize  = sizeof(stageTwoInfo.sender)
                    + sizeof(stageTwoInfo.sender) 
                    + (sizeof(cs::Signature) + sizeof(cs::Hash)) * curTrustedAmount;

  auto memPtr = allocator_.allocateNext(pStageTwoMsgSize + sizeof(cs::RoundNumber));
  uint8_t* rawData = (uint8_t*)memPtr.get();
  uint8_t* ptr = rawData;
  memcpy(ptr, &roundNumber_, sizeof(cs::RoundNumber));
  ptr += sizeof(cs::RoundNumber);
  *ptr= stageTwoInfo.sender;

  ptr += 1;
  *ptr = (uint8_t)curTrustedAmount;
  ptr += 1;

  for (int i = 0; i < curTrustedAmount; i++) {
    memcpy(ptr, stageTwoInfo.signatures.at(i).data(), sizeof(cs::Signature));
    ptr += sizeof(cs::Signature);
    memcpy(ptr, stageTwoInfo.hashes.at(i).data(), sizeof(cs::Hash));
    ptr += sizeof(cs::Hash);
  }

  cscrypto::GenerateSignature(stageTwoInfo.sig, solver_->getPrivateKey(), rawData, pStageTwoMsgSize + sizeof(cs::RoundNumber));

  pStageTwoMessage_[myConfidantIndex_] = std::string(cs::numeric_cast<const char*>((void*)(rawData + sizeof(cs::RoundNumber))), pStageTwoMsgSize);

  sendToConfidants(MsgTypes::SecondStage, roundNumber_, stageTwoInfo.sig, pStageTwoMessage_[myConfidantIndex_]);
  csdebug() << "NODE> " << __func__ << "(): done";
}

void Node::getStageTwoRequest(const uint8_t* data, const size_t size, const cs::PublicKey& requester) {
  LOG_DEBUG(__func__);

  if ((myLevel_ != NodeLevel::Confidant) && (myLevel_ != NodeLevel::Writer)) {
    return;
  }

  istream_.init(data, size);
  uint8_t requesterNumber = 0u;
  uint8_t requiredNumber =0u;
  istream_ >> requesterNumber >> requiredNumber;

  cslog() << "NODE> Getting StageTwo Request from [" << (int)requesterNumber << "] ";
  if (requester != cs::Conveyer::instance().confidants().at(requesterNumber)) {
    return;
  }

  if (!istream_.good() || !istream_.end()) {
    cserror() << "Bad StageTwo packet format";
    return;
  }

  solver_->gotStageTwoRequest(requesterNumber, requiredNumber);
}

void Node::getStageTwo(const uint8_t* data, const size_t size, const cs::PublicKey& sender) {
  csdetails() << "NODE> " << __func__ << "()";
  if ((myLevel_ != NodeLevel::Confidant) && (myLevel_ != NodeLevel::Writer)) {
    csdebug() << "NODE> ignore stage-2 as no confidant";
    return;
  }
  LOG_EVENT( "Getting Stage Two from " << cs::Utils::byteStreamToHex(sender.data(), 32));

  istream_.init(data, size);
  size_t msgSize;
  cs::StageTwo stage;
  istream_ >> stage.sig;
  std::string rawBytes;
  istream_ >> rawBytes;
  if (!istream_.good() || !istream_.end()) {
    cserror() << "Bad StageTwo packet format";
    return;
  }
  const uint8_t* stagePtr = (uint8_t*)rawBytes.data();
  msgSize = rawBytes.size();
  auto memPtr = allocator_.allocateNext(msgSize + sizeof(cs::RoundNumber));
  uint8_t* rawData = (uint8_t*)memPtr.get();
  memcpy(rawData, &roundNumber_, sizeof(cs::RoundNumber));
  memcpy(rawData + sizeof(cs::RoundNumber), stagePtr, msgSize);
  stage.sender = *stagePtr;
  pStageTwoMessage_[stage.sender] = rawBytes;
  // cslog() << "Received message (" << msgSize << ") from [" << (int)stage.sender  << "] :" << cs::Utils::byteStreamToHex((const char*)rawData, msgSize + sizeof(cs::RoundNumber));

  cslog() << __func__  << "(): Sender             :" << cs::Utils::byteStreamToHex(sender.data(), 32);
  const cs::RoundTable* table = cs::Conveyer::instance().roundTable(roundNumber_);

  if (table == nullptr) {
    cserror() << __func__ << ", round table is nullptr";
    return;
  }

  if (table->confidants.size() <= stage.sender) {
    cserror() << __func__ << ", sender index is out of range confidants, "
              << "index " << static_cast<int>(stage.sender) << ", size " << table->confidants.size();
    return;
  }
  
  if (!cscrypto::VerifySignature(stage.sig, table->confidants[stage.sender],
      rawData, msgSize + sizeof(cs::RoundNumber)))
  {
    cslog() << "Stage Two from [" << static_cast<int>(stage.sender) << "] -  WRONG SIGNATURE!!!";
    return;
  }
  cslog() << "Signature is OK";
  rawData += (sizeof(uint8_t) + sizeof(cs::RoundNumber));
  uint8_t trustedAmount = *rawData;
  rawData += sizeof(uint8_t);;
  cs::Signature tempSig;
  cs::Hash tempHash;
  for (int i = 0; i < trustedAmount; i++) {
    memcpy(tempSig.data(), rawData, tempSig.size());
    stage.signatures.push_back(tempSig);
    rawData += sizeof(cs::Signature);
   // cslog() << " Sig [" << i << "]: " << cs::Utils::byteStreamToHex((const char*)stage.signatures.at(i).data(), stage.signatures.at(i).size());
    memcpy(tempHash.data(), rawData, tempHash.size());
    stage.hashes.push_back(tempHash);
    rawData += sizeof(cs::Hash);
   // cslog() << " Hash[" << i << "]: " << cs::Utils::byteStreamToHex((const char*)stage.hashes.at(i).data(), stage.hashes.at(i).size());
  }

  csdebug() << "NODE> Stage Two from [" << (int)stage.sender << "] is OK!";
  solver_->gotStageTwo(std::move(stage));
}

void Node::sendStageThree(cs::StageThree& stageThreeInfo) {
  csdebug() << "NODE> " << __func__ << "()";
#ifdef MYLOG
  cslog() << "NODE> Stage THREE sending";
#endif
  if (myLevel_ != NodeLevel::Confidant) {
    cswarning() << "NODE> Only confidant nodes can send consensus stages";
    return;
  }

  size_t pStageThreeMsgSize = 2 * sizeof(uint8_t) + 3 * sizeof(cs::Hash) + stageThreeInfo.realTrustedMask.size();
  pStageThreeMessage_.clear();
  auto memPtr = allocator_.allocateNext(pStageThreeMsgSize + sizeof(cs::RoundNumber));
  uint8_t* rawData = (uint8_t*)memPtr.get();
  uint8_t* msgPtr = rawData;
  memcpy(rawData, &roundNumber_, sizeof(cs::RoundNumber));
  rawData += sizeof(cs::RoundNumber);

  *rawData = stageThreeInfo.sender;
  rawData += sizeof(uint8_t);
  *rawData = stageThreeInfo.writer;
  rawData += sizeof(uint8_t);
  memcpy(rawData, stageThreeInfo.hashBlock.data(), stageThreeInfo.hashBlock.size());
  rawData += stageThreeInfo.hashBlock.size();
  memcpy(rawData, stageThreeInfo.hashCandidatesList.data(), stageThreeInfo.hashCandidatesList.size());
  rawData += stageThreeInfo.hashCandidatesList.size();
  memcpy(rawData, stageThreeInfo.hashHashesList.data(), stageThreeInfo.hashHashesList.size());
  rawData += stageThreeInfo.hashHashesList.size();
  *rawData = (uint8_t)stageThreeInfo.realTrustedMask.size();
  rawData += sizeof(uint8_t);
  memcpy(rawData, stageThreeInfo.realTrustedMask.data(), stageThreeInfo.realTrustedMask.size());

  cscrypto::GenerateSignature(stageThreeInfo.sig,solver_->getPrivateKey(), msgPtr,pStageThreeMsgSize + sizeof(cs::RoundNumber));
  pStageThreeMessage_[myConfidantIndex_] = std::string(cs::numeric_cast<const char*>((void*)(msgPtr + sizeof(cs::RoundNumber))), pStageThreeMsgSize);
  sendToConfidants(MsgTypes::ThirdStage, roundNumber_, stageThreeInfo.sig, pStageThreeMessage_[myConfidantIndex_]);

  csdebug() << "NODE> " << __func__ << "(): done";
}

void Node::getStageThreeRequest(const uint8_t* data, const size_t size, const cs::PublicKey& requester) {
  csdebug() << "NODE> " << __func__ << "()";
  // cslog() << "NODE> Getting StageThree Request";
  if (myLevel_ != NodeLevel::Confidant) {
    return;
  }
  if (nodeIdKey_ == requester) {
    return;
  }

  istream_.init(data, size);
  uint8_t requesterNumber = 0u;
  uint8_t requiredNumber = 0u;
  istream_ >> requesterNumber >> requiredNumber;

  if (requester != cs::Conveyer::instance().confidants().at(requesterNumber)) {
    return;
  }

  if (!istream_.good() || !istream_.end()) {
    cserror() << "Bad StageThree packet format";
    return;
  }
  solver_->gotStageThreeRequest(requesterNumber, requiredNumber);
}

void Node::getStageThree(const uint8_t* data, const size_t size, const cs::PublicKey& sender) {
  csdetails() << "NODE> " << __func__ << "()";
  if (myLevel_ != NodeLevel::Confidant && myLevel_ != NodeLevel::Writer) {
    csdebug() << "NODE> ignore stage-3 as no confidant";
    return;
  }

  istream_.init(data, size);
  cs::StageThree stage;
  istream_  >> stage.sig;
  std::string rawBytes;
  istream_ >> rawBytes;

  const uint8_t* stagePtr = (uint8_t*)rawBytes.data();
  size_t msgSize = rawBytes.size();
  auto memPtr = allocator_.allocateNext(msgSize + sizeof(cs::RoundNumber));
  uint8_t* rawData = (uint8_t*)memPtr.get();

  memcpy(rawData, &roundNumber_, sizeof(cs::RoundNumber));
  memcpy(rawData + sizeof(cs::RoundNumber), stagePtr, msgSize);
  if (!istream_.good() || !istream_.end()) {
    cserror() << "NODE> Bad StageTwo packet format";
    return;
  }

  //rawData += sizeof(cs::RoundNumber);
  stage.sender = *stagePtr;
  pStageThreeMessage_[stage.sender] = rawBytes ;
  //cslog() << "Received message: "<< byteStreamToHex((const char*)rawData, msgSize);


  if (!cscrypto::VerifySignature(stage.sig, cs::Conveyer::instance().roundTable(roundNumber_)->confidants.at(stage.sender), rawData, msgSize + sizeof(cs::RoundNumber))) {
    cswarning() << "NODE> Stage Three from [" << (int)stage.sender << "] -  WRONG SIGNATURE!!!";
    return;
  }

  rawData += (sizeof(cs::RoundNumber) + sizeof(uint8_t));
  stage.writer = *rawData;
  rawData += sizeof(uint8_t);
  memcpy(stage.hashBlock.data(), rawData, stage.hashBlock.size());
  rawData += stage.hashBlock.size();
  memcpy(stage.hashCandidatesList.data(), rawData, stage.hashCandidatesList.size());
  rawData += stage.hashCandidatesList.size();
  memcpy(stage.hashHashesList.data(), rawData, stage.hashHashesList.size());
  rawData += stage.hashHashesList.size();
  //uint8_t mSize8 = (uint8_t)*rawData;
  size_t mSize = cs::Conveyer::instance().roundTable(roundNumber_)->confidants.size();
  rawData += sizeof(uint8_t);
  std::string realTrustedMask((const char*)rawData, mSize);
  //memcpy(stage.realTrustedMask., realTrustedMask.data(), mSize); -- do not remove before repairing!!!
  //stage.realTrustedMask = realTrustedMask;

  csdebug() << "NODE> Stage Three from [" << (int)stage.sender << "] is OK!";
  solver_->gotStageThree(std::move(stage));
}

void Node::requestStageConsensus(MsgTypes msgType, uint8_t respondent, uint8_t required) {
  if ((myLevel_ != NodeLevel::Confidant) && (myLevel_ != NodeLevel::Writer)) {
    cswarning() << "NODE> Only confidant nodes can request consensus stages";
    return;
  }

  const auto& conveyer = cs::Conveyer::instance();

  if (!conveyer.isConfidantExists(respondent)) {
    return;
  }

  sendDefault(conveyer.confidants().at(respondent), msgType, roundNumber_ , myConfidantIndex_, required);

  csdebug() << "NODE> " << __func__ << "(): done";
}

void Node::prepareMetaForSending(cs::RoundTable& roundTable, std::string timeStamp) {
  csdebug() << "NODE> " << __func__ << "():" << " timestamp = " << timeStamp;
  // only for new consensus
  cs::PoolMetaInfo poolMetaInfo;
  poolMetaInfo.sequenceNumber = blockChain_.getLastWrittenSequence() + 1;  // change for roundNumber
  poolMetaInfo.timestamp = timeStamp;

  /////////////////////////////////////////////////////////////////////////// preparing block meta info
  cs::Conveyer& conveyer = cs::Conveyer::instance();
  cs::PublicKey pk;
  std::fill(pk.begin(), pk.end(), 0);
  std::optional<csdb::Pool> pool = conveyer.applyCharacteristic(poolMetaInfo, pk);// solver_->getPublicKey());
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

void Node::sendRoundTable(cs::RoundTable& roundTable, cs::PoolMetaInfo poolMetaInfo, cs::Signature poolSignature) {
  cs::Conveyer& conveyer = cs::Conveyer::instance();
  roundNumber_ = roundTable.round;
  // update hashes in round table here, they are free of stored packets' hashes
  //if (!roundTable.hashes.empty()) {
  //  roundTable.hashes.clear();
  //}
  //{
  //  cs::SharedLock lock(conveyer.sharedMutex());
  //  for (const auto& element : conveyer.transactionsPacketTable()) {
  //    roundTable.hashes.push_back(element.first);
  //  }
  //}
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

  // for (std::size_t i = 0; i < confidants.size(); ++i) {
  //  const cs::PublicKey& confidant = confidants[i];
  //  if (confidant != table.general) {
  //    cslog() << i << ". " << cs::Utils::byteStreamToHex(confidant.data(), confidant.size());
  //  }
  //}

  const cs::PacketsHashes& hashes = table.hashes;
  cslog() << "Hashes count: " << hashes.size();

  // for (std::size_t i = 0; i < hashes.size(); ++i) {
  //  csdebug() << i << ". " << hashes[i].toString();
  //}

  transport_->clearTasks();

  onRoundStart_V3(table);
  startConsensus();
}

void Node::getRoundTable(const uint8_t* data, const size_t size, const cs::RoundNumber rNum,
                        const cs::PublicKey& sender) {
  csdebug() << "\n";
  cslog() << "NODE> " << __func__ << "():";

  if (myLevel_ == NodeLevel::Writer) {
    cswarning() << "\tWriters don't need ROUNDINFO";
    return;
  }

  istream_.init(data, size);

  // RoundTable evocation
  std::size_t confidantsCount = 0;
  istream_ >> confidantsCount;

  if (confidantsCount == 0) {
    cserror() << "NODE> " << __func__ << "(): Bad confidants count in round table";
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

  onRoundStart_V3(cs::Conveyer::instance().currentRoundTable());
#ifdef SYNCRO
  blockchainSync();
#endif
  reviewConveyerHashes();

  cslog() << "NODE> " << __func__ << "(): done\n";
}

void Node::sendHash_V3(cs::RoundNumber round)
{
  if(monitorNode) {
    // to block request trusted status
    return;
  }

  if(getBlockChain().getLastWrittenSequence() != round - 1) {
    // should not send hash until have got proper block sequence
    return;
  }

  const auto& tmp = getBlockChain().getLastWrittenHash();
  // = personallyDamagedHash();

  cswarning() << "Sending hash " << tmp.to_string() << " to ALL";
  ostream_.init(BaseFlags::Broadcast);
  ostream_ << MsgTypes::BlockHashV3 << round << tmp;
  flushCurrentTasks();
}

void Node::getHash_V3(const uint8_t* data, const size_t size, cs::RoundNumber rNum, const cs::PublicKey& sender) {
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
  const auto& confidants = cs::Conveyer::instance().confidants();
  const auto cnt = cs::numeric_cast<uint8_t>(confidants.size());
  if (respondent < cnt) {
    sendRoundTableRequest(confidants.at(respondent));
  }
  else {
    cserror() << "NODE> cannot request round info, incorrect respondent number";
  }
}

constexpr const uint8_t InvalidTrustedIndex = (uint8_t) -1;

void Node::sendNextRoundRequest()
{
  ostream_.init(0 /*need no flags!*/);
  // 0xFF means we ask for last writer node simply to repeat round info
  ostream_ << MsgTypes::RoundTableRequest << InvalidTrustedIndex;
  flushCurrentTasks();
}

void Node::sendRoundTableRequest(const cs::PublicKey& respondent)
{
  cslog() << "NODE> send request for next round info after #" << roundNumber_;

  // ask for next round info:
  sendDefault(respondent, MsgTypes::RoundTableRequest, roundNumber_ + 1, myConfidantIndex_);
}

void Node::getRoundTableRequest(const uint8_t* data, const size_t size, const cs::RoundNumber rNum,
                               const cs::PublicKey& requester) {
  csdebug() << "NODE> " << __func__;
  if (nodeIdKey_ == requester) {
    return;
  }
  istream_.init(data, size);

  uint8_t requesterNumber;
  istream_ >> requesterNumber;

  if (!istream_.good() || !istream_.end()) {
    cserror() << "NODE> bad RoundInfo request packet format";
    return;
  }

  // special request to re-send again handling
  if(requesterNumber == InvalidTrustedIndex) {
    csdebug() << "NODE> som enode asks for last round info to repeat";
    if(lastSentRoundData_.roundTable.round == rNum) {
      if(tryResendRoundTable(requester, rNum)) {
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
    // return;
  }

  ostream_.init(0 /*need no flags!*/, target);
  ostream_ << MsgTypes::RoundTableReply << roundNumber_ << (has_requested_info ? (uint8_t)1 : (uint8_t)0);
  flushCurrentTasks();
}

bool Node::tryResendRoundTable(std::optional<const cs::PublicKey> /*respondent*/, cs::RoundNumber rNum) {
  if (lastSentRoundData_.roundTable.round != rNum) {
    cswarning() << "NODE> unable to repeat round data #" << rNum;
    return false;
  }
  //TODO: use respondent.value() to send info directly, otherwise broadcast info
  createRoundPackage(lastSentRoundData_.roundTable, lastSentRoundData_.poolMetaInfo, lastSentRoundData_.characteristic,
                     lastSentRoundData_.poolSignature /*, last_sent_round_data.notifications*/);
  flushCurrentTasks();
  cslog() << "NODE> re-send last round info #" << rNum << " to ALL";
          //<< cs::Utils::byteStreamToHex(respondent.data(), respondent.size());
  return true;
}

void Node::getRoundTableReply(const uint8_t* data, const size_t size,
                             const cs::PublicKey& respondent) {
  csdebug() << "NODE> " << __func__;
  if (myLevel_ != NodeLevel::Confidant) {
    return;
  }
  if (nodeIdKey_ == respondent) {
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

void Node::onRoundStart_V3(const cs::RoundTable& roundTable) {
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

  pStageOneMessage_.clear();
  pStageOneMessage_.resize(roundTable.confidants.size());
  pStageTwoMessage_.clear();
  pStageTwoMessage_.resize(roundTable.confidants.size());
  pStageThreeMessage_.clear();
  pStageThreeMessage_.resize(roundTable.confidants.size());

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
    i++;
  }
  cslog() << " Hashes: " << roundTable.hashes.size();
  for (int i=0; i< roundTable.hashes.size(); i++) {
    csdetails() << "[" << i << "] " << cs::Utils::byteStreamToHex(roundTable.hashes.at(i).toBinary().data(), roundTable.hashes.at(i).size());
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
  cs::RoundNumber rnum = cs::Conveyer::instance().currentRoundNumber();
  solver_->gotConveyerSync(rnum);
  transport_->processPostponed(rnum);
  auto lws = getBlockChain().getLastWrittenSequence();
  // claim the trusted role only if have got proper blockchain:
  if (rnum > lws && rnum - lws == 1) {
    sendHash_V3(rnum);
  }
}
