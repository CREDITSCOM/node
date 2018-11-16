#include <sstream>
#include <numeric>
#include <algorithm>
#include <csignal>

#include <solver2/SolverCore.h>

#include <csnode/nodecore.hpp>
#include <csnode/node.hpp>
#include <csnode/conveyer.hpp>
#include <csnode/datastream.hpp>

#include <lib/system/logger.hpp>
#include <lib/system/utils.hpp>

#include <net/transport.hpp>

#include <base58.h>

#include <boost/optional.hpp>
#include <lib/system/progressbar.hpp>

#include <lz4.h>
#include <sodium.h>

#include <snappy.h>
#include <sodium.h>

const unsigned MIN_CONFIDANTS = 3;
const unsigned MAX_CONFIDANTS = 100;

const csdb::Address Node::genesisAddress_ = csdb::Address::from_string("0000000000000000000000000000000000000000000000000000000000000001");
const csdb::Address Node::startAddress_   = csdb::Address::from_string("0000000000000000000000000000000000000000000000000000000000000002");

Node::Node(const Config& config):
  nodeIdKey_(config.getMyPublicKey()),
  bc_(config.getPathToDB().c_str(), genesisAddress_, startAddress_),
  solver_(new slv2::SolverCore(this, genesisAddress_, startAddress_)),
  transport_(new Transport(config, this)),
#ifdef MONITOR_NODE
  stats_(bc_),
#endif
#ifdef NODE_API
  api_(bc_, solver_),
#endif
  allocator_(1 << 24, 5),
  packStreamAllocator_(1 << 26, 5),
  ostream_(&packStreamAllocator_, nodeIdKey_) {
  good_ = init();
}

Node::~Node() {
  delete solver_;
  delete transport_;
}

bool Node::init() {
  if (!transport_->isGood()) {
    return false;
  }

  if (!bc_.isGood()) {
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
  solver_->runSpammer();
#endif

  cs::Connector::connect(&sendingTimer_.timeOut, this, &Node::processTimer);
  cs::Connector::connect(&cs::Conveyer::instance().flushSignal(), this, &Node::onTransactionsPacketFlushed);

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
  cs::Bytes publicKey;
  cs::Bytes privateKey;

  publicKey.resize(PUBLIC_KEY_LENGTH);
  privateKey.resize(PRIVATE_KEY_LENGTH);

  crypto_sign_keypair(publicKey.data(), privateKey.data());

  std::ofstream f_pub(publicKeyFileName_);
  f_pub << EncodeBase58(publicKey);
  f_pub.close();

  std::ofstream f_priv(privateKeyFileName_);
  f_priv << EncodeBase58(privateKey);
  f_priv.close();

  cs::PublicKey fixedPublicKey;
  cs::PrivateKey fixedPrivateKey;

  std::copy(publicKey.begin(), publicKey.end(), fixedPublicKey.begin());
  std::copy(privateKey.begin(), privateKey.end(), fixedPrivateKey.begin());

  return std::make_pair<cs::PublicKey, cs::PrivateKey>(std::move(fixedPublicKey), std::move(fixedPrivateKey));
}

bool Node::checkKeysForSignature(const cs::PublicKey& publicKey, const cs::PrivateKey& privateKey) {
  const uint8_t msg[] = {255, 0, 0, 0, 255};
  cs::Signature signature;

  unsigned long long sig_size;
  crypto_sign_detached(signature.data(), &sig_size, msg, 5, privateKey.data());

  int ver_ok = crypto_sign_verify_detached(signature.data(), msg, 5, publicKey.data());

  if (ver_ok == 0) {
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
#ifdef SYNCRO
  cslog() <<"Node: last written sequence = " << getBlockChain().getLastWrittenSequence();
#endif
  if (!isSyncroStarted_) {
    if (roundNum_ > getBlockChain().getLastWrittenSequence() + 1) {
      isSyncroStarted_ = true;
      roundToSync_ = roundNum_;

      cslog() << "Processing pools sync....";
      sendBlockRequest();
    }
  }
}

void Node::processSyncPools() {
  if (!isSyncroStarted_) {
    return;
  }

  while (true) {
    auto iterator = syncPools_.find(bc_.getLastWrittenSequence() + 1);

    if (iterator == syncPools_.end()) {
      break;
    }

    Node::showSyncronizationProgress(getBlockChain().getLastWrittenSequence(), roundToSync_);

    bc_.onBlockReceived(iterator->second);
    syncPools_.erase(iterator);
  }
}

void Node::addPoolMetaToMap(cs::PoolSyncMeta&& meta, csdb::Pool::sequence_t sequence) {
  if (poolMetaMap_.count(sequence) == 0ul) {
    poolMetaMap_.emplace(sequence, std::move(meta));
    cslog() << "NODE> Put pool meta to temporary storage, await sync complection";
  }
  else {
    cswarning() << "NODE> Can not add same pool meta sequence";
  }
}

void Node::processMetaMap() {
  cslog() << "NODE> Processing pool meta map, map size: " << poolMetaMap_.size();

  for (auto& [sequence, meta] : poolMetaMap_) {
    solver_->countFeesInPool(&meta.pool);
    meta.pool.set_previous_hash(bc_.getLastWrittenHash());

    getBlockChain().finishNewBlock(meta.pool);

    if (meta.pool.verify_signature(std::string(meta.signature.begin(), meta.signature.end()))) {
      csdebug() << "NODE> RECEIVED KEY Writer verification successfull";
      writeBlock(meta.pool, sequence, meta.sender);
    }
    else {
      cswarning() << "NODE> RECEIVED KEY Writer verification failed";
      cswarning() << "NODE> remove wallets from wallets cache";

      getBlockChain().removeWalletsInPoolFromCache(meta.pool);
    }
  }

  poolMetaMap_.clear();
}

void Node::run() {
  transport_->run();
}

void Node::stop() {
  solver_->finish();
  LOG_WARN("[WARNING] : [SOLVER STOPPED]");
  auto bcStorage = bc_.getStorage();
  bcStorage.close();
  LOG_WARN("[WARNING] : [BLOCKCHAIN STORAGE CLOSED]");
}

/* Requests */
void Node::flushCurrentTasks() {
  transport_->addTask(ostream_.getPackets(), ostream_.getPacketsCount());
  ostream_.clear();
}

#ifdef MONITOR_NODE
bool monitorNode = true;
#else
bool monitorNode = false;
#endif

void Node::getRoundTableSS(const uint8_t* data, const size_t size, const cs::RoundNumber rNum, uint8_t type) {
  istream_.init(data, size);

  cslog() << "NODE> Get Round Table";

  if (roundNum_ < rNum || type == MsgTypes::BigBang) {
    roundNum_ = rNum;
  }
  else {
    cswarning() << "Bad round number, ignoring";
    return;
  }

  cs::RoundTable roundTable;

  if (!readRoundData(roundTable)) {
    return;
  }

  if (myLevel_ == NodeLevel::Main) {
    if (!istream_.good()) {
      cswarning() << "Bad round table format, ignoring";
      return;
    }
  }

  transport_->clearTasks();

  if (getBlockChain().getLastWrittenSequence() < roundNum_ - 1) {
    return;
  }

  // start round on node
  onRoundStart(roundTable);
  onRoundStartConveyer(std::move(roundTable));

  // TODO: think how to improve this code
  cslog() << "NODE> Get Round table SS -> got Round = " << rNum;
  cs::Timer::singleShot(TIME_TO_AWAIT_SS_ROUND, [this,rNum]() {
    solver_->gotRound(rNum);
  });
}

void Node::sendRoundTable(const cs::RoundTable& roundTable) {
  ostream_.init(BaseFlags::Broadcast | BaseFlags::Fragmented | BaseFlags::Compressed);
  ostream_ << MsgTypes::RoundTable;
  ostream_ << roundTable.round;
  ostream_ << roundTable.confidants.size();
  ostream_ << roundTable.hashes.size();
  ostream_ << roundTable.general;

  for (const auto& confidant : roundTable.confidants) {
    ostream_ << confidant;
    cslog() << __FUNCTION__ << " confidant: " << cs::Utils::byteStreamToHex(confidant.data(), confidant.size());
  }

  for (const auto& hash : roundTable.hashes) {
    ostream_ << hash;
  }

  cslog() << "------------------------------------------  SendRoundTable  ---------------------------------------";
  cslog() << "Round " << roundNum_ << ", General: " << cs::Utils::byteStreamToHex(roundTable.general.data(), roundTable.general.size()) << "Confidants: ";

  const cs::ConfidantsKeys confidants = roundTable.confidants;

  for (std::size_t i = 0; i < confidants.size(); ++i) {
    const cs::PublicKey& confidant = confidants[i];

    if (confidant != roundTable.general) {
      cslog() << i << ". " << cs::Utils::byteStreamToHex(confidant.data(), confidant.size());
    }
  }

  const cs::Hashes& hashes = roundTable.hashes;
  cslog() << "Hashes count: " << hashes.size();

  for (std::size_t i = 0; i < hashes.size(); ++i) {
    csdebug() << i << ". " << hashes[i].toString();
  }

  flushCurrentTasks();
}

template<typename... Args>
bool Node::sendNeighbours(const cs::PublicKey& target, const MsgTypes& msgType, const cs::RoundNumber round, const Args&... args) {
  ConnectionPtr connection = transport_->getConnectionByKey(target);

  if (connection) {
    sendNeighbours(connection, msgType, round, args...);
  }

  return static_cast<bool>(connection);
}

template<typename... Args>
void Node::sendNeighbours(const ConnectionPtr& target, const MsgTypes& msgType, const cs::RoundNumber round, const Args&... args) {
  ostream_.init(BaseFlags::Neighbours | BaseFlags::Broadcast | BaseFlags::Fragmented | BaseFlags::Compressed);
  ostream_ << msgType << round;

  writeDefaultStream(args...);

  csdebug() << "NODE> Sending data Direct: data size " << ostream_.getCurrentSize();
  csdebug() << "NODE> Sending data Direct: to " << target->getOut();

  transport_->deliverDirect(ostream_.getPackets(), ostream_.getPacketsCount(), target);
  ostream_.clear();
}

template <class... Args>
void Node::sendBroadcast(const MsgTypes& msgType, const cs::RoundNumber round, const Args&... args) {
  ostream_.init(BaseFlags::Broadcast | BaseFlags::Fragmented | BaseFlags::Compressed);
  csdebug() << "NODE> Sending broadcast";

  sendBroadcastImpl(msgType, round, args...);
}

template <class... Args>
void Node::tryToSendDirect(const cs::PublicKey& target, const MsgTypes& msgType, const cs::RoundNumber round, const Args&... args) {
  const bool success = sendNeighbours(target, msgType, round, args...);

  if (!success) {
    sendBroadcast(target, msgType, round, args...);
  }
}

template <class... Args>
bool Node::sendToRandomNeighbour(const MsgTypes& msgType, const cs::RoundNumber round, const Args&... args) {
  ConnectionPtr target = transport_->getRandomNeighbour();

  if (target) {
    sendNeighbours(target, msgType, round, args...);
  }

  return target;
}

void Node::getVector(const uint8_t* data, const size_t size, const cs::PublicKey& sender) {
  if (myLevel_ != NodeLevel::Confidant) {
    return;
  }

  if (nodeIdKey_ == sender) {
    return;
  }

  cslog() << "NODE> Getting vector from " << cs::Utils::byteStreamToHex(sender.data(), sender.size());

  istream_.init(data, size);

  cs::HashVector vec;
  istream_ >> vec;

  cslog() << "Got vector";

  solver_->gotVector(std::move(vec));
}

void Node::sendVector(const cs::HashVector& vector) {
  cslog() << "NODE> 0 Sending vector";

  if (myLevel_ != NodeLevel::Confidant) {
    cserror() << "Only confidant nodes can send vectors";
    return;
  }

  sendBroadcast(MsgTypes::ConsVector, roundNum_, vector);
}

void Node::getMatrix(const uint8_t* data, const size_t size, const cs::PublicKey& sender) {
  if (myLevel_ != NodeLevel::Confidant) {
    return;
  }

  if (nodeIdKey_ == sender) {
    return;
  }

  istream_.init(data, size);

  cs::HashMatrix mat;
  istream_ >> mat;

  cslog() << "NODE> Getting matrix from " << cs::Utils::byteStreamToHex(sender.data(), sender.size());
  cslog() << "Got matrix";

  solver_->gotMatrix(std::move(mat));
}

void Node::sendMatrix(const cs::HashMatrix& matrix) {
  cslog() << "NODE> 0 Sending matrix to ";

  if (myLevel_ != NodeLevel::Confidant) {
    cserror() << "Only confidant nodes can send matrices";
    return;
  }

  sendBroadcast(MsgTypes::ConsMatrix, roundNum_, matrix);
}

uint32_t Node::getRoundNumber() {
  return roundNum_;
}

void Node::getHash(const uint8_t* data, const size_t size, const cs::PublicKey& sender) {
  if (myLevel_ != NodeLevel::Confidant && myLevel_ != NodeLevel::Writer) {
    return;
  }

  cslog() << "Get hash size: " << size;

  istream_.init(data, size);

  csdb::PoolHash poolHash;
  istream_ >> poolHash;

  if (!istream_.good() || !istream_.end()) {
    cswarning() << "Bad hash packet format";
    return;
  }

  solver_->gotHash(std::move(poolHash), sender);
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

void Node::getPacketHashesRequest(const uint8_t* data, const std::size_t size, const cs::RoundNumber round, const cs::PublicKey& sender) {
  istream_.init(data, size);

  std::size_t hashesCount = 0;
  istream_ >> hashesCount;

  csdebug() << "NODE> Get packet hashes request: sender " << cs::Utils::byteStreamToHex(sender.data(), sender.size());

  cs::Hashes hashes;
  hashes.reserve(hashesCount);

  for (std::size_t i = 0; i < hashesCount; ++i) {
    cs::TransactionsPacketHash hash;
    istream_ >> hash;

    hashes.push_back(std::move(hash));
  }

  cslog() << "NODE> Hashes request got hashes count: " << hashesCount;

  if (hashesCount != hashes.size()) {
    cserror() << "Bad hashes created";
    return;
  }

  processPacketsRequest(std::move(hashes), round, sender);
}

void Node::getPacketHashesReply(const uint8_t* data, const std::size_t size, const cs::RoundNumber round, const cs::PublicKey& sender) {
  if (cs::Conveyer::instance().isSyncCompleted(round)) {
    csdebug() << "NODE> Sync packets already synced in round: " << round;
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

void Node::getRoundTable(const uint8_t* data, const size_t size, const cs::RoundNumber round) {
  cslog() << "NODE> RoundTable";

  istream_.init(data, size);

  std::size_t confidantsCount = 0;
  istream_ >> confidantsCount;

  if (confidantsCount == 0) {
    cserror() << "Bad confidants count in round table";
    return;
  }

  std::size_t hashesCount = 0;
  istream_ >> hashesCount;

  cs::RoundTable roundTable;
  roundTable.round = round;

  // to node
  roundNum_ = round;

  cs::PublicKey general;
  istream_ >> general;

  cs::ConfidantsKeys confidants;
  confidants.reserve(confidantsCount);

  for (std::size_t i = 0; i < confidantsCount; ++i) {
    cs::PublicKey key;
    istream_ >> key;

    confidants.push_back(std::move(key));
  }

  cs::Hashes hashes;
  hashes.reserve(hashesCount);

  for (std::size_t i = 0; i < hashesCount; ++i) {
    cs::TransactionsPacketHash hash;
    istream_ >> hash;

    hashes.push_back(hash);
  }

  roundTable.general = std::move(general);
  roundTable.confidants = std::move(confidants);
  roundTable.hashes = std::move(hashes);

  onRoundStart(roundTable);
  onRoundStartConveyer(std::move(roundTable));
  cslog() << "NODE> Get Round table -> got Round =" << round;
  solver_->gotRound(round);
}

void Node::getCharacteristic(const uint8_t* data, const size_t size, const cs::RoundNumber round, const cs::PublicKey& sender) {
  cslog() << "NODE> Characteric has arrived";
  cs::Conveyer& conveyer = cs::Conveyer::instance();

  if (!conveyer.isSyncCompleted(round)) {
    cslog() << "NODE> Packet sync not finished, saving characteristic meta to call after sync";

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
  uint64_t sequence = 0;

  cslog() << "NODE> Characteristic data size: " << size;

  istream_ >> time;
  istream_ >> characteristicMask >> sequence;

  cs::PoolMetaInfo poolMetaInfo;
  poolMetaInfo.sequenceNumber = sequence;
  poolMetaInfo.timestamp = std::move(time);

  cs::Signature signature;
  istream_ >> signature;

  cs::Notifications notifications;
  istream_ >> notifications;

  for (std::size_t i = 0; i < notifications.size(); ++i) {
    conveyer.addNotification(notifications[i]);
  }

  std::vector<cs::Hash> confidantsHashes;

  for (const auto& notification : conveyer.notifications()) {
    cs::Hash hash;
    cs::DataStream notificationStream(notification.data(), notification.size());

    notificationStream >> hash;

    confidantsHashes.push_back(hash);
  }

  cs::Hash characteristicHash = getBlake2Hash(characteristicMask.data(), characteristicMask.size());

  for (const auto& hash : confidantsHashes) {
    if (hash != characteristicHash) {
      cserror() << "NODE> Some of confidants hashes is dirty";
      return;
    }
  }

  cslog() << "NODE> GetCharacteristic " << poolMetaInfo.sequenceNumber << " maskbit count " << characteristicMask.size();
  cslog() << "NODE> Time >> " << poolMetaInfo.timestamp << "  << Time";

  cs::Characteristic characteristic;
  characteristic.mask = std::move(characteristicMask);
  conveyer.setCharacteristic(std::move(characteristic));

  assert(sequence <= this->getRoundNumber());

  cs::PublicKey writerPublicKey;
  istream_ >> writerPublicKey;

  std::optional<csdb::Pool> pool = conveyer.applyCharacteristic(poolMetaInfo, writerPublicKey);

  if (!pool) {
    cserror() << "NODE> Get characteristic, created pool is not valid";
    return;
  }

  if (isSyncroStarted_) {
    cs::PoolSyncMeta meta;
    meta.sender = sender;
    meta.signature = signature;
    meta.pool = std::move(pool).value();

    addPoolMetaToMap(std::move(meta), sequence);

    return;
  }

  solver_->countFeesInPool(&pool.value());
  pool.value().set_previous_hash(bc_.getLastWrittenHash());
  getBlockChain().finishNewBlock(pool.value());

  if (pool.value().verify_signature(std::string(signature.begin(), signature.end()))) {
    cswarning() << "NODE> RECEIVED KEY Writer verification successfull";
    writeBlock_V3(pool.value(), sequence, sender);
  }
  else {
    cswarning() << "NODE> RECEIVED KEY Writer verification failed";
    cswarning() << "NODE> remove wallets from wallets cache";
    getBlockChain().removeWalletsInPoolFromCache(pool.value());
  }
}

void Node::onTransactionsPacketFlushed(const cs::TransactionsPacket& packet) {
  CallsQueue::instance().insert(std::bind(&Node::sendTransactionsPacket, this, packet));
}

void Node::writeBlock(csdb::Pool& newPool, size_t sequence, const cs::PublicKey& sender) {
  csdebug() << "GOT NEW BLOCK: global sequence = " << sequence;

  if (sequence > this->getRoundNumber()) {
    return;  // remove this line when the block candidate signing of all trusted will be implemented
  }

  this->getBlockChain().setGlobalSequence(cs::numeric_cast<uint32_t>(sequence));

#ifndef MONITOR_NODE
  if (sequence == (this->getBlockChain().getLastWrittenSequence() + 1)) {
    this->getBlockChain().putBlock(newPool);

    if ((this->getNodeLevel() != NodeLevel::Writer) && (this->getNodeLevel() != NodeLevel::Main)) {
      auto poolHash = this->getBlockChain().getLastWrittenHash();
      sendHash(poolHash, sender);

      cslog() << "SENDING HASH to writer: " << poolHash.to_string();
    }
    else {
      cslog() << "I'm node " << this->getNodeLevel() << " and do not send hash";
    }
  }
  else {
    cswarning() << "NODE> Can not write block with sequence " << sequence;
  }
#else
  if (sequence == (this->getBlockChain().getLastWrittenSequence() + 1)) {
    this->getBlockChain().putBlock(newPool);
  } else {
    solver_->gotIncorrectBlock(std::move(newPool), sender);
  }
#endif
}

void Node::writeBlock_V3(csdb::Pool& newPool, size_t sequence, const cs::PublicKey& sender) {
  csdebug() << "GOT NEW BLOCK: global sequence = " << sequence;

  if (sequence > this->getRoundNumber()) {
    return;  // remove this line when the block candidate signing of all trusted will be implemented
  }

  this->getBlockChain().setGlobalSequence(cs::numeric_cast<uint32_t>(sequence));

#ifndef MONITOR_NODE
  if (sequence == (this->getBlockChain().getLastWrittenSequence() + 1)) {
    this->getBlockChain().putBlock(newPool);

    if (this->getNodeLevel() != NodeLevel::Writer) {
      auto poolHash = this->getBlockChain().getLastWrittenHash();
      sendHash_V3(roundNum_ +1);

      cslog() << "SENDING HASH to ALL";
    }
    else {
      cslog() << "I'm node " << this->getNodeLevel() << " and do not send hash";
    }
  }
  else {
    cswarning() << "NODE> Can not write block with sequence " << sequence;
  }
#else
  if (sequence == (this->getBlockChain().getLastWrittenSequence() + 1)) {
    this->getBlockChain().putBlock(newPool);
  }
  else {
    solver_->gotIncorrectBlock(std::move(newPool), sender);
  }
#endif
}

void Node::getWriterNotification(const uint8_t* data, const std::size_t size, const cs::PublicKey& sender) {
  istream_.init(data, size);

  cs::Bytes notification;
  istream_ >> notification;

  if (!isCorrectNotification(notification.data(), notification.size())) {
    cswarning() << "NODE> Notification failed " << cs::Utils::byteStreamToHex(sender.data(), sender.size());
    return;
  }

  cs::Conveyer& conveyer = cs::Conveyer::instance();
  conveyer.addNotification(notification);

  if (conveyer.isEnoughNotifications(cs::Conveyer::NotificationState::Equal) && myLevel_ == NodeLevel::Writer) {
    cslog() << "NODE> Confidants count more then 51%";
    applyNotifications();
  }
}

void Node::applyNotifications() {
  csdebug() << "NODE> Apply notifications";

  cs::PoolMetaInfo poolMetaInfo;
  poolMetaInfo.sequenceNumber = bc_.getLastWrittenSequence() + 1;
  poolMetaInfo.timestamp = cs::Utils::currentTimestamp();

  cs::Conveyer& conveyer = cs::Conveyer::instance();
  std::optional<csdb::Pool> pool = conveyer.applyCharacteristic(poolMetaInfo, solver_->getPublicKey());
  if (!pool) {
    cserror() << "NODE> APPLY CHARACTERISTIC ERROR!";
    return;
  }

  solver_->countFeesInPool(&pool.value());
  pool.value().set_previous_hash(bc_.getLastWrittenHash());
  getBlockChain().finishNewBlock(pool.value());

  //TODO: need to write confidants notifications bytes to csdb::Pool user fields
  #ifdef MONITOR_NODE
  cs::Solver::addTimestampToPool(pool.value()));
  #endif

  pool.value().sign(solver_->getPrivateKey());

  // array
  cs::Signature poolSignature;
  const auto& signature = pool.value().signature();
  std::copy(signature.begin(), signature.end(), poolSignature.begin());

  csdebug() << "NODE> ApplyNotification " << " Signature: " << cs::Utils::byteStreamToHex(poolSignature.data(), poolSignature.size());

  const bool isVerified = pool.value().verify_signature();
  cslog() << "NODE> After sign: isVerified == " << isVerified;

  writeBlock(pool.value(), poolMetaInfo.sequenceNumber, cs::PublicKey());

  cslog() << "NODE> send characteristic of size " << conveyer.characteristic().mask.size();
  createBlockValidatingPacket(poolMetaInfo, conveyer.characteristic(), poolSignature, conveyer.notifications());

  flushCurrentTasks();
}

bool Node::isCorrectNotification(const uint8_t* data, const std::size_t size) {
  cs::DataStream stream(data, size);

  cs::Hash characteristicHash;
  stream >> characteristicHash;

  cs::Conveyer& conveyer = cs::Conveyer::instance();
  cs::Hash currentCharacteristicHash = conveyer.characteristicHash();

  if (characteristicHash != currentCharacteristicHash) {
    csdebug() << "NODE> Characteristic equals failed";
    csdebug() << "NODE> Received characteristic - " << cs::Utils::byteStreamToHex(characteristicHash.data(), characteristicHash.size());
    csdebug() << "NODE> Writer solver chracteristic - " << cs::Utils::byteStreamToHex(currentCharacteristicHash.data(), currentCharacteristicHash.size());
    return false;
  }

  cs::PublicKey writerPublicKey;
  stream >> writerPublicKey;

  if (writerPublicKey != nodeIdKey_) {
    csdebug() << "NODE> Writer public key equals failed";
    return false;
  }

  cs::Signature signature;
  stream >> signature;

  cs::PublicKey publicKey;
  stream >> publicKey;

  std::size_t messageSize = size - signature.size() - publicKey.size();

  if (!cs::Utils::verifySignature(signature, publicKey, data, messageSize)) {
    cserror() << "NODE> Writer verify signature notification failed";

    csdebug() << "Data: " << cs::Utils::byteStreamToHex(data, messageSize) << " verification failed";
    csdebug() << "Signature: " << cs::Utils::byteStreamToHex(signature.data(), signature.size());

    return false;
  }

  return true;
}

void Node::createBlockValidatingPacket(const cs::PoolMetaInfo& poolMetaInfo,
                                            const cs::Characteristic& characteristic,
                                            const cs::Signature& signature,
                                            const cs::Notifications& notifications) {
  ostream_.init(BaseFlags::Broadcast | BaseFlags::Compressed | BaseFlags::Fragmented);
  ostream_ << MsgTypes::NewCharacteristic << roundNum_;
  ostream_ << poolMetaInfo.timestamp;
  ostream_ << characteristic.mask;
  ostream_ << poolMetaInfo.sequenceNumber;
  ostream_ << signature;
  ostream_ << notifications;
  ostream_ << solver_->getPublicKey();
}

void Node::createRoundPackage(const cs::RoundTable& roundTable,
  const cs::PoolMetaInfo& poolMetaInfo,
  const cs::Characteristic& characteristic,
  const cs::Signature& signature,
  const cs::Notifications& notifications) {

  ostream_.init(BaseFlags::Broadcast | BaseFlags::Compressed | BaseFlags::Fragmented);
  ostream_ << MsgTypes::RoundInfo << roundNum_;
  ostream_ << roundTable.confidants.size();
  ostream_ << roundTable.hashes.size();
  for (const auto& confidant : roundTable.confidants) {
    ostream_ << confidant;
  }
  for (const auto& hash : roundTable.hashes) {
    ostream_ << hash;
  }
  ostream_ << poolMetaInfo.timestamp;
  if(!characteristic.mask.empty()) {
      cslog() << "NODE> packing " << characteristic.mask.size() << " bytes of char. mask to send";
  }
  ostream_ << characteristic.mask;
  ostream_ << poolMetaInfo.sequenceNumber;
  ostream_ << signature;
  ostream_ << notifications;
  ostream_ << solver_->getPublicKey();
}


void Node::sendWriterNotification() {
  cs::PublicKey writerPublicKey = solver_->getWriterPublicKey();

  ostream_.init(BaseFlags::Compressed | BaseFlags::Fragmented, writerPublicKey);
  ostream_ << MsgTypes::WriterNotification;
  ostream_ << roundNum_;
  ostream_ << createNotification(writerPublicKey);

  cslog() << "NODE> Notification sent to writer";

  flushCurrentTasks();
}

cs::Bytes Node::createNotification(const cs::PublicKey& writerPublicKey) {
  cs::Hash characteristicHash = cs::Conveyer::instance().characteristicHash();

  cs::Bytes bytes;
  cs::DataStream stream(bytes);

  stream << characteristicHash << writerPublicKey;

  cs::Signature signature = cs::Utils::sign(bytes.data(), bytes.size(), solver_->getPrivateKey());

  stream << signature;
  stream << solver_->getPublicKey();

  return bytes;
}

void Node::sendHash(const csdb::PoolHash& hash, const cs::PublicKey& target) {
  if (myLevel_ == NodeLevel::Writer || myLevel_ == NodeLevel::Main) {
    cserror() << "Writer and Main node shouldn't send hashes";
    return;
  }

  cslog() << "NODE> Sending hash of " << roundNum_ << " to " << cs::Utils::byteStreamToHex(target.data(), target.size());
  cslog() << "NODE> Hash is " << hash.to_string();

  ostream_.init(BaseFlags::Fragmented, target);
  ostream_ << MsgTypes::BlockHash << roundNum_ << hash;

  flushCurrentTasks();
}

void Node::sendTransactionsPacket(const cs::TransactionsPacket& packet) {
  if (packet.hash().isEmpty()) {
    cswarning() << "Send transaction packet with empty hash failed";
    return;
  }

  ostream_.init(BaseFlags::Compressed | BaseFlags::Fragmented | BaseFlags::Broadcast);
  ostream_ << MsgTypes::TransactionPacket << roundNum_ << packet;

  flushCurrentTasks();
}

void Node::sendPacketHashesRequest(const cs::Hashes& hashes, const cs::RoundNumber round) {
  if (myLevel_ == NodeLevel::Writer) {
    cserror() << "Writer should has all transactions hashes";
    return;
  }

  csdebug() << "NODE> Sending packet hashes request: " << hashes.size();

  const auto msgType = MsgTypes::TransactionsPacketRequest;
  const auto& general = cs::Conveyer::instance().roundTable().general;

  const bool sendToGeneral = sendNeighbours(general, msgType, round, hashes);

  if (!sendToGeneral) {
    csdebug() << "NODE> Sending transaction packet request: Cannot get a connection with a general ";
    sendPacketHashesRequestToRandomNeighbour(hashes, round);
  }
}

void Node::sendPacketHashesRequestToRandomNeighbour(const cs::Hashes& hashes, const cs::RoundNumber round) {
  if (cs::Conveyer::instance().isSyncCompleted(round)) {
    return;
  }

  const auto msgType = MsgTypes::TransactionsPacketRequest;
  const auto neighboursCount = transport_->getMaxNeighbours();
  const auto hashesCount = hashes.size();
  const bool isHashesLess = hashesCount < neighboursCount;
  const std::size_t remainderHashes = isHashesLess ? 0 : hashesCount % neighboursCount;
  const std::size_t amountHashesOfRequest = isHashesLess ? hashesCount : (hashesCount / neighboursCount);

  auto getRequestHashesClosure = [hashes](const std::size_t startHashNumber, std::size_t hashesCount) {
    cs::Hashes hashesToSend;

    csdebug() << "NODE> Sending transaction packet request to Random Neighbour: hashes Count: " << hashesCount;

    for (auto i = 0; i < hashesCount; ++i) {
      hashesToSend.push_back(hashes[startHashNumber + i]);
    }

    return hashesToSend;
  };

  bool successRequest = false;

  for (std::size_t i = 0; i < neighboursCount; ++i) {
    const std::size_t count = i == (neighboursCount - 1) ? amountHashesOfRequest + remainderHashes : amountHashesOfRequest;

    successRequest = sendToRandomNeighbour(msgType, round, getRequestHashesClosure(i * amountHashesOfRequest, count));

    if (!successRequest) {
      cswarning() << "NODE> Sending transaction packet request: Cannot get a connection with a random neighbour";
      break;
    }

    if (isHashesLess) {
      break;
    }
  }

  if (!successRequest) {
    sendBroadcast(msgType, round, getRequestHashesClosure(0, hashesCount));
    return;
  }

  cs::Timer::singleShot(cs::PacketHashesRequestDelay, [round, this] {
                          const cs::Conveyer& conveyer = cs::Conveyer::instance();
                          if (!conveyer.isSyncCompleted(round)) {
                            auto neededHashes = conveyer.neededHashes(round);
                            if (neededHashes) {
                              sendPacketHashesRequestToRandomNeighbour(*neededHashes, round);
                            }
                          };
                       });
}

void Node::sendPacketHashesReply(const cs::Packets& packets, const cs::RoundNumber round, const cs::PublicKey& target) {
  if (packets.empty()) {
    return;
  }

  csdebug() << "NODE> Sending transaction packet reply: packets count: " << packets.size();

  const auto msgType = MsgTypes::TransactionsPacketReply;
  const bool success = sendNeighbours(target, msgType, round, packets);

  if (!success) {
    csdebug() << "NODE> Sending transaction packet reply: Cannot get a connection with a specified public key " << cs::Utils::byteStreamToHex(target.data(), target.size());

    sendBroadcast(target, msgType, round, packets);
  }
}

void Node::resetNeighbours() {
  transport_->resetNeighbours();
}

void Node::sendBlockRequest() {
  static uint32_t lfReq, lfTimes;

  csdb::Pool::sequence_t sequence = getBlockChain().getLastWrittenSequence() + 1;
  uint32_t reqSeq = sequence;

  if (lfReq != sequence) {
    lfReq = sequence;
    lfTimes = 0;
  }

  while (reqSeq) {
    bool alreadyRequested = false;
    ConnectionPtr requestee = transport_->getSyncRequestee(reqSeq, alreadyRequested);

    if (!requestee) {
      break;  // No more free requestees
    }

    if (!alreadyRequested) {  // Already requested this block from this guy?
      cslog() << "NODE> Sending request for block " << reqSeq << " from nbr " << requestee->id;

      ostream_.init(BaseFlags::Neighbours | BaseFlags::Signed);
      ostream_ << MsgTypes::BlockRequest << roundNum_ << reqSeq;

      transport_->deliverDirect(ostream_.getPackets(), ostream_.getPacketsCount(), requestee);

      if (lfReq == reqSeq && ++lfTimes >= 4) {
        transport_->sendBroadcast(ostream_.getPackets());
      }

      ostream_.clear();
    }

    reqSeq = cs::numeric_cast<uint32_t>(solver_->getNextMissingBlock(reqSeq));
  }

  isAwaitingSyncroBlock_ = true;
  awaitingRecBlockCount_ = 0;

  csdebug() << "SEND BLOCK REQUEST> Sending request for block: " << sequence;
}

void Node::getBlockRequest(const uint8_t* data, const size_t size, const cs::PublicKey& sender) {
  uint32_t requested_seq = 0u;

  istream_.init(data, size);
  istream_ >> requested_seq;

  cslog() << "GET BLOCK REQUEST> Getting the request for block: " << requested_seq;

  if (requested_seq > getBlockChain().getLastWrittenSequence()) {
    cslog() << "GET BLOCK REQUEST> The requested block: " << requested_seq << " is BEYOND my CHAIN";
    return;
  }

  csdb::Pool pool = bc_.loadBlock(bc_.getHashBySequence(requested_seq));

  if (pool.is_valid()) {
    auto prev_hash = csdb::PoolHash::from_string("");
    pool.set_previous_hash(prev_hash);

    sendBlockReply(pool, sender);
  }
}

void Node::getBlockReply(const uint8_t* data, const size_t size) {
  cslog() << __func__;

  csdb::Pool pool;

  istream_.init(data, size);
  istream_ >> pool;

  cslog() << "GET BLOCK REPLY> Getting block " << pool.sequence();

  transport_->syncReplied(cs::numeric_cast<uint32_t>(pool.sequence()));

  if (getBlockChain().getGlobalSequence() < pool.sequence()) {
    getBlockChain().setGlobalSequence(cs::numeric_cast<uint32_t>(pool.sequence()));
  }

  if (pool.sequence() == bc_.getLastWrittenSequence() + 1) {
    cslog() << "GET BLOCK REPLY> Block Sequence is Ok";

    if (roundToSync_ > 0) {
      Node::showSyncronizationProgress(getBlockChain().getLastWrittenSequence(), roundToSync_);
    }

    bc_.onBlockReceived(pool);
    solver_->gotBlockReply(pool);
    isAwaitingSyncroBlock_ = false;
  }
  else {
    syncPools_[pool.sequence()] = std::move(pool);
    cswarning() << "GET NO NEXT SYNC POOL";
  }

  processSyncPools();

  if (roundToSync_ != bc_.getLastWrittenSequence()) {
    sendBlockRequest();
  }
  else {
    isSyncroStarted_ = false;
    roundToSync_ = 0;

    processMetaMap();
    cslog() << "POOL SYNCRO FINISHED";
  }
}

void Node::sendBlockReply(const csdb::Pool& pool, const cs::PublicKey& target) {
  ConnectionPtr conn = transport_->getConnectionByKey(target);
  if (!conn) {
    cswarning() << "Cannot get a connection with a specified public key";
    return;
  }

  tryToSendDirect(target, MsgTypes::RequestedBlock, roundNum_, pool);
}

void Node::becomeWriter() {
  myLevel_ = NodeLevel::Writer;
  cslog() << "NODE> Became writer";

  if (cs::Conveyer::instance().isEnoughNotifications(cs::Conveyer::NotificationState::GreaterEqual)) {
    applyNotifications();
  }
}

void Node::onRoundStart(const cs::RoundTable& roundTable) {
  cslog() << "======================================== ROUND " << roundTable.round
          << " ========================================";
  cslog() << "Node PK = " << cs::Utils::byteStreamToHex(nodeIdKey_.data(), nodeIdKey_.size());

  const cs::ConfidantsKeys& confidants = roundTable.confidants;

  if (roundTable.general == nodeIdKey_) {
    myLevel_ = NodeLevel::Main;
  }
  else {
    const auto iter = std::find(confidants.begin(), confidants.end(), nodeIdKey_);

    if (iter != confidants.end()) {
      myLevel_ = NodeLevel::Confidant;
      myConfidantIndex_ = cs::numeric_cast<uint8_t>(std::distance(confidants.begin(), iter));
    }
    else {
      myLevel_ = NodeLevel::Normal;
    }
  }

  // Pretty printing...
  cslog() << "Round " << roundTable.round << " started. Mynode_type := " << myLevel_ << " Confidants: ";

  for (std::size_t i = 0; i < confidants.size(); ++i) {
    const auto& confidant = confidants[i];
    cslog() << i << ". " << cs::Utils::byteStreamToHex(confidant.data(), confidant.size());
  }

  const cs::Hashes& hashes = roundTable.hashes;

  cslog() << "Transaction packets hashes count: " << hashes.size();

  for (std::size_t i = 0; i < hashes.size(); ++i) {
    csdebug() << i << ". " << hashes[i].toString();
  }

#ifdef SYNCRO
  blockchainSync();
#endif

  if (!sendingTimer_.isRunning()) {
    cslog() << "NODE> Transaction timer started";
    sendingTimer_.start(cs::TransactionsPacketInterval);
  }

  // TODO: think now to improve this code
  solver_->nextRound();

  // TODO: check if this removes current tasks? if true - thats bad
  transport_->processPostponed(roundNum_);
}

void Node::processPacketsRequest(cs::Hashes&& hashes, const cs::RoundNumber round, const cs::PublicKey& sender) {
  csdebug() << "NODE> Processing packets sync request";

  cs::Packets packets;

  const auto& conveyer = cs::Conveyer::instance();

  for (const auto& hash : hashes) {
    std::optional<cs::TransactionsPacket> packet = conveyer.searchPacket(hash, round);

    if (packet) {
      packets.push_back(std::move(packet).value());
    }
  }

  if (packets.size()) {
    csdebug() << "NODE> Found hashes count in hash table storage: " << packets.size();
  }
  else {
    csdebug() << "NODE> Can not find round in storage, hash not found";
  }

  sendPacketHashesReply(packets, round, sender);
}

void Node::processPacketsReply(cs::Packets&& packets, const cs::RoundNumber round) {
  csdebug() << "NODE> Processing packets reply";

  cs::Conveyer& conveyer = cs::Conveyer::instance();

  for (auto&& packet : packets) {
    conveyer.addFoundPacket(round, std::move(packet));
  }

  if (conveyer.isSyncCompleted(round)) {
    csdebug() << "NODE> Packets sync completed";
    resetNeighbours();
    cslog() << "NODE> processPacketsReply -> got Round";
    solver_->gotRound(round);
    transport_->processPostponed(roundNum_);

    if (auto meta = conveyer.characteristicMeta(round); meta.has_value()) {
      csdebug() << "NODE> Run characteristic meta";
      getCharacteristic(meta->bytes.data(), meta->bytes.size(), round-1, meta->sender);
    }
  }
}

void Node::processTransactionsPacket(cs::TransactionsPacket&& packet) {
  cs::Conveyer::instance().addTransactionsPacket(packet);
}

void Node::onRoundStartConveyer(cs::RoundTable&& roundTable) {
  cs::Conveyer& conveyer = cs::Conveyer::instance();
  conveyer.setRound(std::move(roundTable));
  const auto& rt = conveyer.roundTable();

  if(rt.hashes.empty() || conveyer.isSyncCompleted()) {
      if(rt.hashes.empty()) {
          cslog() << "NODE> No hashes in round table - > got round now";
      }
      else {
          cslog() << "NODE> All hashes in conveyer -> got round now";
      }
      solver_->gotRound(rt.round);
      transport_->processPostponed(roundNum_);
  }
  else {
    //TODO: whether possible roundNum_ != rt.round at this point?
    sendPacketHashesRequest(conveyer.currentNeededHashes(), roundNum_);
  }
}

bool Node::getSyncroStarted() {
  return isSyncroStarted_;
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

void Node::initNextRound(std::vector<cs::PublicKey>&& confidantNodes)
{
  cslog() <<"Node: init next round1";
    // copied from Solver::gotHash():
    cs::Hashes hashes;
    cs::Conveyer& conveyer = cs::Conveyer::instance();
    cs::RoundNumber round = conveyer.currentRoundNumber();

    {
        cs::SharedLock lock(conveyer.sharedMutex());
        for(const auto& element : conveyer.transactionsPacketTable()) {
            hashes.push_back(element.first);
        }
    }

    cs::RoundTable table;
    table.round = ++round;
    table.confidants = std::move(confidantNodes);
    //table.general = mainNode;
   
    table.hashes = std::move(hashes);
    conveyer.setRound(std::move(table));

    initNextRound(conveyer.roundTable());
}

void Node::initNextRound(const cs::RoundTable& roundTable) {
  roundNum_ = roundTable.round;
  sendRoundTable(roundTable);
  cslog() << "NODE> RoundNumber :" << roundNum_;
  onRoundStart(roundTable);
}

Node::MessageActions Node::chooseMessageAction(const cs::RoundNumber rNum, const MsgTypes type) {
  if (type == MsgTypes::NewCharacteristic && rNum <= roundNum_) {
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
    return (rNum > roundNum_ ? MessageActions::Process : MessageActions::Drop);
  }

  if (type == MsgTypes::RoundInfo) {
    return (rNum > roundNum_ ? MessageActions::Process : MessageActions::Drop);
  }

  if (type == MsgTypes::BigBang && rNum > getBlockChain().getLastWrittenSequence()) {
    return MessageActions::Process;
  }

  if (type == MsgTypes::RoundTableRequest) {
    return (rNum < roundNum_ ? MessageActions::Process : MessageActions::Drop);
  }

  if (type == MsgTypes::RoundTableSS) {
    return (rNum > roundNum_ ? MessageActions::Process : MessageActions::Drop);
  }

  if (type == MsgTypes::BlockRequest || type == MsgTypes::RequestedBlock) {
    return (rNum <= roundNum_ ? MessageActions::Process : MessageActions::Drop);
  }

  if(type == MsgTypes::BlockHashV3) {
      if(rNum == roundNum_ && cs::Conveyer::instance().isSyncCompleted()) {
          return MessageActions::Process;
      }
      return MessageActions::Postpone;
  }

  if (rNum < roundNum_) {
    return type == MsgTypes::NewBlock ? MessageActions::Process : MessageActions::Drop;
  }

  return (rNum == roundNum_ ? MessageActions::Process : MessageActions::Postpone);
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

  cslog() << "NODE> RoundNumber: " << roundNum_;

  roundTable.confidants = std::move(confidants);
  roundTable.general = mainNode;
  roundTable.round = roundNum_;
  roundTable.hashes.clear();

  return true;
}

void Node::showSyncronizationProgress(csdb::Pool::sequence_t lastWrittenSequence, csdb::Pool::sequence_t globalSequence) {
  if (globalSequence == 0) {
    return;
  }
  auto last = float(lastWrittenSequence);
  auto global = float(globalSequence);
  const float maxValue = 100.0f;
  const uint32_t syncStatus = cs::numeric_cast<uint32_t>((1.0f - (global - last) / global) * maxValue);
  if (syncStatus <= maxValue) {
    ProgressBar bar;
    cslog() << "SYNC: " << bar.string(syncStatus);
  }
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

std::ostream& operator<< (std::ostream& os, NodeLevel nodeLevel) {
  os << nodeLevelToString(nodeLevel);
  return os;
}

template <typename T, typename... Args>
void Node::writeDefaultStream(const T& value, const Args&... args) {
  ostream_ << value;
  writeDefaultStream(args...);
}

template<typename T>
void Node::writeDefaultStream(const T& value) {
  ostream_ << value;
}

template <typename... Args>
void Node::sendBroadcast(const cs::PublicKey& target, const MsgTypes& msgType, const cs::RoundNumber round, const Args&... args) {
  ostream_.init(BaseFlags::Fragmented | BaseFlags::Compressed, target);
  csdebug() << "NODE> Sending broadcast to: " << cs::Utils::byteStreamToHex(target.data(), target.size());

  sendBroadcastImpl(msgType, round, args...);
}

template <typename... Args>
void Node::sendBroadcastImpl(const MsgTypes& msgType, const cs::RoundNumber round, const Args&... args) {
  ostream_ << msgType << round;

  writeDefaultStream(args...);

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
  LOG_DEBUG(__func__);
  if (myLevel_ != NodeLevel::Confidant) {
    cswarning() <<"Only confidant nodes can send consensus stages";
    return;
  }
  LOG_DEBUG( "Round = " << roundNum_
    << " Sender: " << (int)stageOneInfo.sender << std::endl
    << " Hash: " << cs::Utils::byteStreamToHex(stageOneInfo.hash.data(), stageOneInfo.hash.size())
    << " Cand Amount: " << (int)stageOneInfo.candidatesAmount);
    

  
  size_t msgSize = 34 + 32 * stageOneInfo.candidatesAmount;//hash size + 2*sizeof(uint8_t) + sizeof(PublicKey) * ...
  auto memPtr = allocator_.allocateNext(msgSize);
  uint8_t* rawData = (uint8_t*)memPtr.get();

  *rawData = stageOneInfo.sender;
  memcpy(rawData + 1, stageOneInfo.hash.data(), stageOneInfo.hash.size());
  *(rawData + 33) = stageOneInfo.candidatesAmount;
  for (int i = 0; i < stageOneInfo.candidatesAmount; i++) {
    memcpy(rawData + 34 + 32 * i, stageOneInfo.candiates[i].data(), stageOneInfo.candiates[i].size());
  }
  //cslog() << "Sent message: (" << msgSize << ") : " << byteStreamToHex((const char*)rawData, msgSize);
  
  unsigned long long sig_size;
  crypto_sign_ed25519_detached(stageOneInfo.sig.data(), &sig_size, rawData, msgSize, solver_->getPrivateKey().data());
  //cslog() << " Sig: " << byteStreamToHex((const char*)stageOneInfo.sig.val, 64);
  ostream_.init(BaseFlags::Broadcast);
  ostream_ << MsgTypes::FirstStage
    << roundNum_
    << msgSize
    << stageOneInfo.sig;
  ostream_ << std::string(cs::numeric_cast<const char*>((void*)rawData), msgSize);
  allocator_.shrinkLast(msgSize);
  csdebug() << "done" ;
  flushCurrentTasks();
}

// sends StageOne request to respondent about required
void Node::requestStageOne(uint8_t respondent, uint8_t required) {
#ifdef MYLOG
  cslog() << "NODE> Stage ONE requesting ... ";
#endif
  if (myLevel_ != NodeLevel::Confidant) {
    cswarning() << "Only confidant nodes can request consensus stages";
    //return;
  }

  ostream_.init(0/*need no flags!*/, cs::Conveyer::instance().roundTable().confidants.at(respondent));
  ostream_ << MsgTypes::FirstStageRequest
      << roundNum_
      << myConfidantIndex_
      << required;
   flushCurrentTasks();
  LOG_DEBUG("done");
}

void Node::getStageOneRequest(const uint8_t* data, const size_t size, const cs::PublicKey& requester)
{
  LOG_DEBUG(__func__);
  //cslog() << "NODE> Getting StageOne Request";
  if (myLevel_ != NodeLevel::Confidant) {
    return;
}
  // cslog() << "NODE> Getting StageOne 0";
  if (nodeIdKey_ == requester) {
    return;
  }
  //cslog() << "NODE> Getting StageOne 1";
  //cslog() << "Getting Stage One Request from " << byteStreamToHex(sender.str, 32));
  istream_.init(data, size);
  uint8_t requesterNumber;
  uint8_t requiredNumber;
  istream_ >> requesterNumber >> requiredNumber;

  if(requester != cs::Conveyer::instance().roundTable().confidants.at(requesterNumber)) {
      return;
  }
  if (!istream_.good() || !istream_.end()) {
    cslog() << "Bad StageOne packet format";
    return;
  }
  solver_->gotStageOneRequest(requesterNumber, requiredNumber);
}

void Node::sendStageOneReply(const cs::StageOne& stageOneInfo, const uint8_t requester) {
#ifdef MYLOG
  cslog() << "NODE> Stage ONE Reply sending ... ";
#endif
  if (myLevel_ != NodeLevel::Confidant) {
    cswarning() << "Only confidant nodes can send consensus stages";
    //return;
  }
 /*sizeof(uint8_t) + sizeof(Hash) + sizeof(uint8_t) + sizeof(PublicKey) * candidatesAmount*/
  size_t msgSize = 34 + 32 * stageOneInfo.candidatesAmount;
  auto memPtr = allocator_.allocateNext(msgSize);
  uint8_t* rawData = (uint8_t*)memPtr.get();

  *rawData = stageOneInfo.sender;
  memcpy(rawData + 1, stageOneInfo.hash.data(), stageOneInfo.hash.size());
  *(rawData + 33) = stageOneInfo.candidatesAmount;
  for (int i = 0; i < stageOneInfo.candidatesAmount; i++) {
    memcpy(rawData + 34 + 32 * i, stageOneInfo.candiates[i].data(), stageOneInfo.candiates[i].size());
    //cslog() << i << ". " << byteStreamToHex(stageOneInfo.candiates[i].str, 32);
  }

  ostream_.init(0/*need no flags!*/, cs::Conveyer::instance().roundTable().confidants.at(requester));

  ostream_ << MsgTypes::FirstStage
    << roundNum_
    << msgSize
    << stageOneInfo.sig;
  ostream_ << std::string(cs::numeric_cast<const char *>((void*) rawData), msgSize);
  csdebug() << "Round = " << roundNum_
    << " Sender: " << (int)stageOneInfo.sender << std::endl
    << " Hash: " << cs::Utils::byteStreamToHex(stageOneInfo.hash.data(), stageOneInfo.hash.size())
    << " Cand Amount: " << (int)(stageOneInfo.candidatesAmount)
    << " Sig: " << cs::Utils::byteStreamToHex(stageOneInfo.sig.data(), stageOneInfo.sig.size());

  for (int i = 0; i < stageOneInfo.candidatesAmount; i++) {
    ostream_ << stageOneInfo.candiates[i];
    csdebug() << i << ". " << cs::Utils::byteStreamToHex(stageOneInfo.candiates[i].data(), stageOneInfo.candiates[i].size());
  }
  flushCurrentTasks();
  allocator_.shrinkLast(msgSize);
  csdebug() << "done";
}

void Node::getStageOne(const uint8_t* data, const size_t size, const cs::PublicKey& sender) {
  csdebug() << __func__;
  //cslog() << "NODE> Getting StageOne";
  if (myLevel_ != NodeLevel::Confidant) {
    return;
  }
 // cslog() << "NODE> Getting StageOne 0";
  if (nodeIdKey_ == sender) {
    return;
  }
  istream_.init(data, size);
  size_t msgSize;
  cs::StageOne stage;
  istream_  >> msgSize
            >> stage.sig;
           
  std::string raw_bytes;
  istream_ >> raw_bytes;
  if(!istream_.good() || !istream_.end()) {
      cserror() <<"Bad StageOne packet format";
      return;
  }

  const uint8_t* stagePtr = (const uint8_t*) raw_bytes.data();

  auto memPtr = allocator_.allocateNext(msgSize);
  uint8_t* rawData = (uint8_t*)memPtr.get();

  memcpy(rawData, stagePtr, msgSize);

   //cslog() << "Received message: "<< byteStreamToHex((const char*)stagePtr , msgSize);

  stage.sender = *rawData;
  if (crypto_sign_ed25519_verify_detached(stage.sig.data(), rawData,
    msgSize, (const unsigned char*) cs::Conveyer::instance().roundTable().confidants.at(stage.sender).data())) {
    cslog() << "NODE> Stage One from [" << (int)stage.sender << "] -  WRONG SIGNATURE!!!" ;
      return;
  }
  //cslog() << "NODE> Signature is OK!" ;
  memcpy(stage.hash.data(), rawData + 1, 32);
  stage.candidatesAmount = *(rawData + 33);
  for (int i = 0; i < stage.candidatesAmount; i++) {
    memcpy(stage.candiates[i].data(), rawData + 34 + 32 * i, 32);
    cslog() << i << ". " << cs::Utils::byteStreamToHex(stage.candiates[i].data(), stage.candiates[i].size());
  }
  allocator_.shrinkLast(msgSize);
  LOG_DEBUG( "Size: " << msgSize << "  Sender: " << (int)stage.sender << std::endl
    << " Hash: " << cs::Utils::byteStreamToHex(stage.hash.data(), stage.hash.size()) 
    << " Cand Amount: " << (int)stage.candidatesAmount << std::endl
    << " Sig: " << cs::Utils::byteStreamToHex(stage.sig.data(), stage.sig.size()));
    
  solver_->gotStageOne(std::move(stage));
}

void Node::sendStageTwo(const cs::StageTwo& stageTwoInfo)
{
#ifdef MYLOG
  cslog() << "NODE> +++++++++++++++++++++++ Stage TWO sending +++++++++++++++++++++++++++";
#endif
  if ((myLevel_ != NodeLevel::Confidant) && (myLevel_ != NodeLevel::Writer)) {
    cswarning() <<"Only confidant nodes can send consensus stages";
    return;
  }

  size_t msgSize = 2 + 64 * stageTwoInfo.trustedAmount;
  auto memPtr = allocator_.allocateNext(msgSize);
  uint8_t* rawData = (uint8_t*)memPtr.get();

  *rawData = stageTwoInfo.sender;
  *(rawData + 1) = stageTwoInfo.trustedAmount;
  for (int i = 0; i < stageTwoInfo.trustedAmount; i++) {
    memcpy(rawData + 2 + 64 * i, stageTwoInfo.signatures[i].data(), stageTwoInfo.signatures[i].size());
  }
  //cslog() << "Sent message: (" << msgSize << ") : " << byteStreamToHex((const char*)rawData, msgSize;

  unsigned long long sig_size;
  crypto_sign_ed25519_detached((unsigned char*)stageTwoInfo.sig.data(), &sig_size, rawData, msgSize, solver_->getPrivateKey().data());

  ostream_.init(BaseFlags::Broadcast);
  ostream_ << MsgTypes::SecondStage
    << roundNum_
    << msgSize
    << stageTwoInfo.sig;
  ostream_ << std::string(cs::numeric_cast<const char*>((void*) rawData), msgSize);
  
 // cslog()<< "NODE> Sending StageTwo:";
  //for (int i = 0; i < stageTwoInfo.trustedAmount; i++) {
  //  cslog() << " Sig[" << i << "]: " << byteStreamToHex((const char*)stageTwoInfo.signatures[i].val, 64);
  //}
  allocator_.shrinkLast(msgSize);
  LOG_DEBUG("done");
  flushCurrentTasks();
}

void Node::requestStageTwo(uint8_t respondent, uint8_t required)
{
  if ((myLevel_ != NodeLevel::Confidant) && (myLevel_ != NodeLevel::Writer)) {
    cswarning() <<"Only confidant nodes can request consensus stages";
    return;
  }
  //#ifdef MYLOG
  cslog() << "==============================";
  cslog() << "NODE> Stage TWO requesting ... ";
  cslog() << "==============================";
  //#endif

  ostream_.init(0/*need no flags!*/, cs::Conveyer::instance().roundTable().confidants.at(respondent));

  ostream_ << MsgTypes::SecondStageRequest
      << roundNum_
      << myConfidantIndex_
      << required;
  flushCurrentTasks();
  LOG_DEBUG("done");
}

void Node::getStageTwoRequest(const uint8_t* data, const size_t size, const cs::PublicKey& requester)
{
  LOG_DEBUG(__func__);

  if ((myLevel_ != NodeLevel::Confidant) && (myLevel_ != NodeLevel::Writer)) {
    return;
  }
  if (nodeIdKey_ == requester) {
    return;
  }
  //LOG_EVENT(FILE_NAME_ << "Getting Stage Two Request from " << byteStreamToHex(sender.str, 32));
  istream_.init(data, size);
  uint8_t requesterNumber;
  uint8_t requiredNumber;
  istream_ >> requesterNumber >> requiredNumber;

  cslog() << "NODE> Getting StageTwo Request from [" << (int)requesterNumber <<"] " ;
  if (requester != cs::Conveyer::instance().roundTable().confidants.at(requesterNumber)) return;

  if (!istream_.good() || !istream_.end()) {
    cserror() <<"Bad StageTwo packet format";
    return;
  }
  solver_->gotStageTwoRequest(requesterNumber, requiredNumber);
}

void Node::sendStageTwoReply(const cs::StageTwo& stageTwoInfo, const uint8_t requester)
{
//#ifdef MYLOG
  cslog() << "NODE> Stage Two REPLY sendiing";
//#endif
  if ((myLevel_ != NodeLevel::Confidant) && (myLevel_ != NodeLevel::Writer)) {
    cswarning() <<"Only confidant nodes can send consensus stages";
    return;
  }
  size_t msgSize = 2 + 64 * stageTwoInfo.trustedAmount;
  auto memPtr = allocator_.allocateNext(msgSize);
  uint8_t* rawData = (uint8_t*)memPtr.get();

  *rawData = stageTwoInfo.sender;
  *(rawData + 1) = stageTwoInfo.trustedAmount;
  for (int i = 0; i < stageTwoInfo.trustedAmount; i++) {
    memcpy(rawData + 2 + 64 * i, stageTwoInfo.signatures[i].data(), stageTwoInfo.signatures[i].size());
  }
  //cslog() << "Sent message: (" << msgSize << ") : " << byteStreamToHex((const char*)rawData, msgSize);

  ostream_.init(0/*need no flags!*/, cs::Conveyer::instance().roundTable().confidants.at(requester));

  ostream_ << MsgTypes::SecondStage
    << roundNum_
    << msgSize
    << stageTwoInfo.sig;
  ostream_ << std::string(cs::numeric_cast<const char*>((void*) rawData), msgSize);
  //cslog() << "NODE> Sending StageTwo:";
  //for (int i = 0; i < stageTwoInfo.trustedAmount; i++) {
  //cslog() << " Sig[" << i << "]: " << byteStreamToHex((const char*)stageTwoInfo.signatures[i].val, 64);
  //}
  allocator_.shrinkLast(msgSize);
  LOG_DEBUG("done");
  flushCurrentTasks();
}

void Node::getStageTwo(const uint8_t* data, const size_t size, const cs::PublicKey& sender)
{
    LOG_DEBUG(__func__);
    //cslog() << "NODE> Getting StageTwo";
  if ((myLevel_ != NodeLevel::Confidant) && (myLevel_ != NodeLevel::Writer)) {
    return;
  }
  if (nodeIdKey_ == sender) {
    return;
  }
  //LOG_EVENT(FILE_NAME_ << "Getting Stage Two from " << byteStreamToHex(sender.str, 32));

  istream_.init(data, size);
  size_t msgSize;
  cs::StageTwo stage;
  istream_ >> msgSize
           >> stage.sig;

  std::string raw_bytes;
  istream_ >> raw_bytes;
  if(!istream_.good() || !istream_.end()) {
      cserror() <<"Bad StageTwo packet format";
      return;
  }

  const uint8_t* stagePtr = (uint8_t*) raw_bytes.data();
  auto memPtr = allocator_.allocateNext(msgSize);
  uint8_t* rawData = (uint8_t*)memPtr.get();
  memcpy(rawData, stagePtr, msgSize);

  stage.sender = *rawData;
  //cslog() << "Received message: "<< byteStreamToHex((const char*)stagePtr, msgSize);
  if (crypto_sign_ed25519_verify_detached(stage.sig.data(), rawData,
    msgSize, (const unsigned char*) cs::Conveyer::instance().roundTable().confidants.at(stage.sender).data()))
  {
    cslog() << "NODE> Stage Two from ["<< (int)stage.sender << "] -  WRONG SIGNATURE!!!";
    return;
  }
  
  //cslog() << "NODE> Stage Two [" << (int)stage.sender << "] signature is OK!";
 //cslog() << "Package Signature: " << byteStreamToHex((const char*)stage.sig.val, 64);
  stage.trustedAmount = *(rawData + 1);
  for (int i = 0; i < stage.trustedAmount; i++) {
    memcpy(stage.signatures[i].data(), rawData + 2 + 64 * i, 64);
    //cslog() << " Sig[" << i << "]: " << byteStreamToHex((const char*)stage.signatures[i].val, 64);
  }

  allocator_.shrinkLast(msgSize);
  solver_->gotStageTwo(std::move(stage));
}

void Node::sendStageThree(const cs::StageThree& stageThreeInfo)
{
    LOG_DEBUG(__func__);
#ifdef MYLOG
    cslog() << "NODE> Stage THREE sending";
#endif
  if (myLevel_ != NodeLevel::Confidant) {
    cswarning() <<"Only confidant nodes can send consensus stages";
    return;
  }

  size_t msgSize = 66; // = 2*sizeof(uint8_t) + 2*sizeof(Hash);
  auto memPtr = allocator_.allocateNext(msgSize);
  uint8_t* rawData = (uint8_t*)memPtr.get();

  *rawData = stageThreeInfo.sender;
  *(rawData + 1) = stageThreeInfo.writer;
  memcpy(rawData + 2 , stageThreeInfo.hashBlock.data(), 32);
  memcpy(rawData + 34, stageThreeInfo.hashCandidatesList.data(), 32);

  unsigned long long sig_size;
  crypto_sign_ed25519_detached((unsigned char*)stageThreeInfo.sig.data(), &sig_size, rawData, msgSize, solver_->getPrivateKey().data());

  ostream_.init(BaseFlags::Broadcast);
  ostream_ << MsgTypes::ThirdStage
    << roundNum_
    << msgSize
    << stageThreeInfo.sig;
  ostream_ << std::string(cs::numeric_cast<const char*>((void*) rawData), msgSize);
  allocator_.shrinkLast(msgSize);
  LOG_DEBUG("done");
  flushCurrentTasks();
}

void Node::requestStageThree(uint8_t respondent, uint8_t required)
{
#ifdef MYLOG
  cslog() << "NODE> Stage THREE requesting ... ";
#endif
  if (myLevel_ != NodeLevel::Confidant) {
    cswarning() <<"Only confidant nodes can request consensus stages";
    //return;
  }

  ostream_.init(0/*need no flags!*/, cs::Conveyer::instance().roundTable().confidants.at(respondent));

  ostream_ << MsgTypes::ThirdStageRequest
      << roundNum_
      << myConfidantIndex_
      << required;
  flushCurrentTasks();
  LOG_DEBUG("done");
}

void Node::getStageThreeRequest(const uint8_t* data, const size_t size, const cs::PublicKey& requester)
{
  LOG_DEBUG(__func__);
  //cslog() << "NODE> Getting StageThree Request";
  if (myLevel_ != NodeLevel::Confidant) {
    return;
  }
  if (nodeIdKey_ == requester) {
    return;
  }
  //LOG_EVENT(FILE_NAME_ << "Getting Stage Three Request from " << byteStreamToHex(sender.str, 32));
  istream_.init(data, size);
  uint8_t requesterNumber;
  uint8_t requiredNumber;
  istream_ >> requesterNumber >> requiredNumber;

  if (requester != cs::Conveyer::instance().roundTable().confidants.at(requesterNumber)) return;

  if (!istream_.good() || !istream_.end()) {
    cserror() <<"Bad StageThree packet format";
    return;
  }
  solver_->gotStageThreeRequest(requesterNumber, requiredNumber);
}

void Node::sendStageThreeReply(const cs::StageThree& stageThreeInfo, const uint8_t requester)
{
  LOG_DEBUG(__func__);
#ifdef MYLOG
  cslog() << "NODE> Stage THREE Reply sending";
#endif
  if (myLevel_ != NodeLevel::Confidant) {
    cswarning() <<"Only confidant nodes can send consensus stages";
    return;
  }

  size_t msgSize = 2 * sizeof(uint8_t) + 2 * sizeof(cs::Hash);
  auto memPtr = allocator_.allocateNext(msgSize);
  uint8_t* rawData = (uint8_t*)memPtr.get();

  *rawData = stageThreeInfo.sender;
  *(rawData + 1) = stageThreeInfo.writer;
  memcpy(rawData + 2, stageThreeInfo.hashBlock.data(), 32);
  memcpy(rawData + 34, stageThreeInfo.hashCandidatesList.data(), 32);

  ostream_.init(0/*need no flags!*/, cs::Conveyer::instance().roundTable().confidants.at(requester));

  ostream_ << MsgTypes::ThirdStage
    << roundNum_
    << msgSize
    << stageThreeInfo.sig;
  ostream_ << std::string(cs::numeric_cast<const char*>((void*) rawData), msgSize);
  allocator_.shrinkLast(msgSize);
  LOG_DEBUG("done");
  flushCurrentTasks();
}

void Node::getStageThree(const uint8_t* data, const size_t size, const cs::PublicKey& sender) {
    LOG_DEBUG(__func__);
  if (myLevel_ != NodeLevel::Confidant && myLevel_ != NodeLevel::Writer) {
    return;
  }
  if (nodeIdKey_ == sender) {
    return;
  }
  //cslog()<< "NODE> Getting Stage Three  ";
  //LOG_EVENT(FILE_NAME_ << "Getting Stage Three from " << byteStreamToHex(sender.str, 32));
  size_t msgSize;
  istream_.init(data, size);
  cs::StageThree stage;
  istream_ >> msgSize
           >> stage.sig;
  std::string raw_bytes;
  istream_ >> raw_bytes;
  const uint8_t* stagePtr = (uint8_t*) raw_bytes.data();
  auto memPtr = allocator_.allocateNext(msgSize);
  uint8_t* rawData = (uint8_t*)memPtr.get();
  memcpy(rawData, stagePtr, msgSize);

  if (!istream_.good() || !istream_.end()) {
    cserror() <<"Bad StageTwo packet format";
    return;
  }
  stage.sender = *rawData;

  //cslog() << "Received message: "<< byteStreamToHex((const char*)rawData, msgSize);
  if (crypto_sign_ed25519_verify_detached(stage.sig.data(), rawData,
    msgSize, (const unsigned char*)cs::Conveyer::instance().roundTable().confidants.at(stage.sender).data())) {
    cslog() << "NODE> Stage Three from ["<< (int)stage.sender << "] -  WRONG SIGNATURE!!!";
    return;
  }
  //cslog() << "NODE> Signature is OK!";
  stage.writer = *(rawData +1);
  memcpy(stage.hashBlock.data(), rawData + 2 , 32);
  memcpy(stage.hashCandidatesList.data(), rawData + 34, 32);
  allocator_.shrinkLast(msgSize);
  solver_->gotStageThree(std::move(stage));
}



void Node::sendRoundInfo(cs::RoundTable& roundTable) {

  csdebug() << "NODE> Apply notifications";
 // only for new consensus
  cs::PoolMetaInfo poolMetaInfo;
  poolMetaInfo.sequenceNumber = bc_.getLastWrittenSequence() + 1; // change for roundNumber
  poolMetaInfo.timestamp = cs::Utils::currentTimestamp();

  /////////////////////////////////////////////////////////////////////////// preparing block meta info
  cs::Conveyer& conveyer = cs::Conveyer::instance();
  std::optional<csdb::Pool> pool = conveyer.applyCharacteristic(poolMetaInfo, solver_->getPublicKey());
  if (!pool) {
    cserror() << "NODE> APPLY CHARACTERISTIC ERROR!";
    return;
  }

  solver_->countFeesInPool(&pool.value());
  pool.value().set_previous_hash(bc_.getLastWrittenHash());
  getBlockChain().finishNewBlock(pool.value());

  //TODO: need to write confidants notifications bytes to csdb::Pool user fields
#ifdef MONITOR_NODE
  cs::Solver::addTimestampToPool(pool.value()));
#endif

  pool.value().sign(solver_->getPrivateKey());

  // array
  cs::Signature poolSignature;
  const auto& signature = pool.value().signature();
  std::copy(signature.begin(), signature.end(), poolSignature.begin());

  csdebug() << "NODE> ApplyNotification " << " Signature: " << cs::Utils::byteStreamToHex(poolSignature.data(), poolSignature.size());

  const bool isVerified = pool.value().verify_signature();
  cslog() << "NODE> After sign: isVerified == " << isVerified;

  writeBlock_V3(pool.value(), poolMetaInfo.sequenceNumber, cs::PublicKey());


  roundNum_ = roundTable.round;
  // update hashes in round table here, they are free of stored packets' hashes
  if(!roundTable.hashes.empty()) {
    roundTable.hashes.clear();
  }
  {
    cs::SharedLock lock(conveyer.sharedMutex());
    for (const auto& element : conveyer.transactionsPacketTable()) {
      roundTable.hashes.push_back(element.first);
    }
  }
  const auto block_characteristic = conveyer.characteristic();
  conveyer.setRound(std::move(roundTable));
  /////////////////////////////////////////////////////////////////////////// sending round info and block
  createRoundPackage(conveyer.roundTable(), poolMetaInfo, block_characteristic, poolSignature, conveyer.notifications());
  flushCurrentTasks();

  /////////////////////////////////////////////////////////////////////////// screen output
  cslog() << "------------------------------------------  SendRoundTable  ---------------------------------------";
  cslog() << "Round " << roundNum_ << ", Confidants: ";
  const cs::RoundTable& table = conveyer.roundTable();
  const cs::ConfidantsKeys confidants = table.confidants;

  for (std::size_t i = 0; i < confidants.size(); ++i) {
    const cs::PublicKey& confidant = confidants[i];

    if (confidant != table.general) {
      cslog() << i << ". " << cs::Utils::byteStreamToHex(confidant.data(), confidant.size());
    }
  }

  //const cs::Hashes& hashes = table.hashes;
  //cslog() << "Hashes count: " << hashes.size();

  //for (std::size_t i = 0; i < hashes.size(); ++i) {
  //  csdebug() << i << ". " << hashes[i].toString();
  //}

 
  transport_->clearTasks();

  //TODO: обновить таблицу раунда в cs::Conveyer::instance()
  //cs::Conveyer::instance().roundTable().confidants.assign(confidantNodes.cbegin(), confidantNodes.cend());
  assert(false);

  onRoundStart_V3(table);

 solver_->gotRound(roundNum_);

 //TODO: need or not?
 transport_->processPostponed(roundNum_);
}

void Node::getRoundInfo(const uint8_t * data, const size_t size, const cs::RoundNumber rNum, const cs::PublicKey& sender) {
  LOG_DEBUG(__func__);
  cslog() << "NODE> Get ROUND INFO ... start processing";
  if (myLevel_ == NodeLevel::Writer) {
    cswarning() <<"NODE> Writers don't need ROUNDINFO";
    return;
  }
  //LOG_EVENT(FILE_NAME_ << "Getting RoundInfo from " << byteStreamToHex(sender.str, 32));

  istream_.init(data, size);

  //RoundTable evocation
  std::size_t confidantsCount = 0;
  istream_ >> confidantsCount;

  if (confidantsCount == 0) {
    cserror() << "Bad confidants count in round table";
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
    istream_>> key;

    confidants.push_back(std::move(key));
  }

  cs::Hashes hashes;
  hashes.reserve(hashesCount);

  for (std::size_t i = 0; i < hashesCount; ++i) {
    cs::TransactionsPacketHash hash;
    istream_ >> hash;

    hashes.push_back(hash);
  }

  roundTable.confidants = std::move(confidants);
  roundTable.hashes = std::move(hashes);

  const cs::ConfidantsKeys confidants_ = roundTable.confidants;
  cslog() << "Node> confidants: " << confidants_.size();
  ///////////////////////////////////// Round table received 


  ///////////////////////////////////// Parcing char func

  cslog() << "NODE> Characteric has arrived";
  cs::Conveyer& conveyer = cs::Conveyer::instance();

  // rNum has been incremented just before:
  if (!conveyer.isSyncCompleted(rNum-1)) {
    cslog() << "NODE> Packet sync not finished, saving characteristic meta to call after sync";

    cs::Bytes characteristicBytes;
    characteristicBytes.assign(istream_.getCurrentPtr(), istream_.getEndPtr());

    cs::CharacteristicMeta meta;
    meta.bytes = std::move(characteristicBytes);
    meta.sender = sender;

    conveyer.addCharacteristicMeta(rNum-1, std::move(meta));
    // no return, perform some more actions at the end
  }
  else {
      std::string time;
      cs::Bytes characteristicMask;
      csdb::Pool::sequence_t sequence = 0;

      cslog() << "NODE> getCharacteristic(): conveyer sync completed, parsing data size " << size;

      istream_ >> time;
      istream_ >> characteristicMask >> sequence;

      cs::PoolMetaInfo poolMetaInfo;
      poolMetaInfo.sequenceNumber = sequence;
      poolMetaInfo.timestamp = std::move(time);

      cs::Signature signature;
      istream_ >> signature;

      std::size_t notificationsSize;
      istream_ >> notificationsSize;

      if(notificationsSize == 0) {
          cserror() << "NODE> getCharacteristic(): notifications count is zero";
      }

      for(std::size_t i = 0; i < notificationsSize; ++i) {
          cs::Bytes notification;
          istream_ >> notification;

          conveyer.addNotification(notification);
      }

      cs::PublicKey writerPublicKey;
      istream_ >> writerPublicKey;

      std::vector<cs::Hash> confidantsHashes;

      for(const auto& notification : conveyer.notifications()) {
          cs::Hash hash;
          cs::DataStream notificationStream(notification.data(), notification.size());

          notificationStream >> hash;

          confidantsHashes.push_back(hash);
      }

      cs::Hash characteristicHash = getBlake2Hash(characteristicMask.data(), characteristicMask.size());

      for(const auto& hash : confidantsHashes) {
          if(hash != characteristicHash) {
              cserror() << "NODE> Some of confidants hashes is dirty";
              return;
          }
      }

      cslog() << "NODE> getCharacteristic(): sequence " << poolMetaInfo.sequenceNumber << ", mask size " << characteristicMask.size();
      csdebug() << "NODE> getCharacteristic(): time = " << poolMetaInfo.timestamp;

      cs::Characteristic characteristic;
      characteristic.mask = std::move(characteristicMask);

      assert(sequence <= this->getRoundNumber());
      ////////////////////////////////////////////////////////////////////////////////////////////////////

      conveyer.setCharacteristic(characteristic);
      std::optional<csdb::Pool> pool = conveyer.applyCharacteristic(poolMetaInfo, writerPublicKey);

      if(isSyncroStarted_) {
          if(pool) {
              cs::PoolSyncMeta meta;
              meta.sender = sender;
              meta.signature = signature;
              meta.pool = std::move(pool).value();

              addPoolMetaToMap(std::move(meta), sequence);
          }
      }

      if(pool) {
          solver_->countFeesInPool(&pool.value());
          pool.value().set_previous_hash(bc_.getLastWrittenHash());
          getBlockChain().finishNewBlock(pool.value());

          if(pool.value().verify_signature(std::string(signature.begin(), signature.end()))) {
              cswarning() << "NODE> RECEIVED KEY Writer verification successfull";
              writeBlock_V3(pool.value(), sequence, sender);
          }
          else {
              cswarning() << "NODE> RECEIVED KEY Writer verification failed";
              cswarning() << "NODE> remove wallets from wallets cache";
              getBlockChain().removeWalletsInPoolFromCache(pool.value());
          }
      }

  }
  onRoundStart_V3(roundTable);
  blockchainSync();

  cslog() << "NODE> Finishing Writing block";
  //for (int i = 0; i < roundTable.confidants.size(); i++) {
  //  cslog() << i << ". " << cs::Utils::byteStreamToHex(roundTable.confidants.at(i).data(), confidants.at(i).size());
  //}

  onRoundStartConveyer(std::move(roundTable));
  // defer until solver_->gotRound() called
  /*transport_->processPostponed(roundNum_);*/
}

void Node::sendHash_V3(cs::RoundNumber round) {
  /* if (myLevel_ == NodeLevel::Writer || myLevel_ == NodeLevel::Main) {
     cserror() <<"Writer and Main node shouldn't send hashes";
     return;
   }*/

  const auto& tmp = getBlockChain().getLastWrittenHash();
  //cs::Hash testHash;
  //std::copy(tmp.cbegin(), tmp.cend(), testHash.begin());
 
  cswarning() <<"Sending hash of " << tmp.to_string() << " to ALL";
  ostream_.init(BaseFlags::Broadcast);
  ostream_ << MsgTypes::BlockHashV3
    << round
    << tmp;
  flushCurrentTasks();
}

void Node::getHash_V3(const uint8_t* data, const size_t size, cs::RoundNumber rNum, const cs::PublicKey& sender) {
  if (myLevel_ != NodeLevel::Confidant) {
    return;
  }

  cslog() << "Get hash of round " << rNum << ", data size " << size;

  istream_.init(data, size);


  csdb::PoolHash tmp;
  istream_ >> tmp;

  if (!istream_.good() || !istream_.end()) {
    cswarning() << "Bad hash packet format";
    return;
  }

  solver_->gotHash(std::move(tmp), sender);
}

void Node::sendRoundInfoRequest(uint8_t respondent)
{
#ifdef MYLOG
  cslog() << "NODE> Stage THREE requesting ... ";
#endif
  if (myLevel_ != NodeLevel::Confidant) {
    cswarning() <<"Only confidant nodes can request consensus stages";
    //return;
  }

  ostream_.init(0/*need no flags!*/, cs::Conveyer::instance().roundTable().confidants.at(respondent));

  ostream_ << MsgTypes::RoundInfoRequest
    << roundNum_
    << myConfidantIndex_;
;
  flushCurrentTasks();
  csdebug() << "done";
}

void Node::getRoundInfoRequest(const uint8_t* data, const size_t size, const cs::RoundNumber rNum, const cs::PublicKey& requester)
{
  csdebug() << __func__;
  //std::cout << "NODE> Getting StageThree Request" << std::endl;
  //if (myLevel_ != NodeLevel::Confidant || myLevel_ != NodeLevel::Writer) {
  //  return;
  //}
  if (nodeIdKey_ == requester) {
    return;
  }
  //LOG_EVENT(FILE_NAME_ << "Getting RoundInfo Request from " << byteStreamToHex(sender.str, 32));
  istream_.init(data, size);

  uint8_t requesterNumber;
  istream_ >> requesterNumber;

  //if (requester != confidantNodes_.at(requesterNumber)) return; - this will not fullfill when the round changed
  auto& confidantNodes_ = cs::Conveyer::instance().roundTable().confidants;

  if (!istream_.good() || !istream_.end()) {
    cserror() << "Bad RoundInfo Request packet format";
    return;
  }
  if (rNum == roundNum_) {
    sendRoundInfoReply(requester, 0 , requesterNumber);
    return;
  }
  else {
    bool found = false;
    uint8_t cNum = confidantNodes_.size();
    for (int i = 0; i < cNum; i++) {
      if (requester == confidantNodes_.at(i)) {
        found = true;
      }
    }
    if (!found) {
      sendRoundInfoReply(requester, 1,requesterNumber);
      return;
    }
    }

  /*if (isConf(requester)) {
    sendRoundInfoReply(requesterNumber, 1);
    return;
  }
*/
  solver_->gotRoundInfoRequest(requester);
}

void Node::sendRoundInfoReply(const cs::PublicKey& requester, uint8_t reply, uint8_t reqNumber)
{

  cslog() << "NODE> Sending RoundInfoRely to [" << (int) reqNumber << "]";

  if (myLevel_ != NodeLevel::Confidant) {
    cswarning() <<"Only confidant nodes can request consensus stages";
    //return;
  }

  ostream_.init(0/*need no flags!*/, requester);

  ostream_ << MsgTypes::RoundInfoReply
    << roundNum_
    << reply;

  flushCurrentTasks();
  LOG_DEBUG("done");
}

void Node::getRoundInfoReply(const uint8_t* data, const size_t size, const cs::RoundNumber rNum, const cs::PublicKey& respondent)
{
  LOG_DEBUG(__func__);
  //cslog() << "NODE> Getting StageThree Request";
  if (myLevel_ != NodeLevel::Confidant) {
    return;
  }
  if (nodeIdKey_ == respondent) {
    return;
  }
  //LOG_EVENT(FILE_NAME_ << "Getting RoundInfo Request from " << byteStreamToHex(sender.str, 32));
  istream_.init(data, size);

  uint8_t reply;
  istream_ >> reply;

  if (!istream_.good() || !istream_.end()) {
    cserror() <<"Bad StageThree packet format";
    return;
  }
  //if (respondent != cs::Conveyer::instance().roundTable().confidants.at(requesterNumber)) return;

  solver_->gotRoundInfoReply(reply, respondent);
}

void Node::onRoundStart_V3(const cs::RoundTable& roundTable)
{
    //if (!solver_->mPoolClosed())
    //{
    //  solver_->sendTL();
    //}
    roundNum_ = roundTable.round;
    cslog() << "======================================== ROUND " << roundNum_ << " ========================================";
    cslog() << "Node PK = " << cs::Utils::byteStreamToHex(nodeIdKey_.data(), nodeIdKey_.size());
    //if(getBlockChain().getGlobalSequence()!= roundNum_-1) getBlockChain().setGlobalSequence(roundNum_-1);


    bool found = false;
    uint8_t conf_no = 0;

    const auto& conf_nodes = roundTable.confidants;

    for(auto& conf : roundTable.confidants) {
        if(conf == nodeIdKey_) {
            myLevel_ = NodeLevel::Confidant;
            myConfidantIndex_ = conf_no;
            cslog() << "===================== NODE LEVEL SET TO CONFIDANT ============================";
            found = true;
            //solver_->initConfRound();
            break;
        }
        conf_no++;
    }

    if(!found) {
        myLevel_ = NodeLevel::Normal;
        cslog() << "===================== NODE LEVEL SET TO NORMAL ============================" ;
    }




    // Pretty printing...
    cslog() << "Round " << roundNum_ << " started. Mynode_type: " << myLevel_
        << std::endl << "Confidants: " ;
    int i = 0;
    for(auto& e : roundTable.confidants) {
      cslog() << i << ". " << cs::Utils::byteStreamToHex(e.data(), e.size());
      i++;
    }
   
    solver_->nextRound();
    
}

void Node::addCompressedPoolToPack(const csdb::Pool& pool)
{
    uint32_t bSize;
    const void* data = const_cast<csdb::Pool&>(pool).to_byte_stream(bSize);
    auto max = LZ4_compressBound(bSize);
    auto memPtr = allocator_.allocateNext(max);
    auto realSize = LZ4_compress_default((const char*) data, (char*) memPtr.get(), bSize, memPtr.size());
    allocator_.shrinkLast(realSize);
    //uint32_t required_size = sizeof(bSize) + sizeof(realSize) + realSize;
    ostream_ << (uint32_t) bSize;
    ostream_ << (uint32_t) realSize;
    ostream_ << std::string(cs::numeric_cast<const char *>(memPtr.get()), realSize);
}

//csdb::Pool Node::getCompressedPoolFromPack()
//{
//    csdb::Pool pool{};
//
//    uint32_t uncompressedSize;
//    uint32_t compressedSize;
//    istream_ >> uncompressedSize >> compressedSize;
//
//    //TODO: review that condition (2) is legal
//    constexpr size_t abnormal_len = 1 << 20;
//    //TODO: how to get avail bytes from istream?
//    if(uncompressedSize >= abnormal_len) {
//        // data is corrupted
//        //ptr_ = end_;
//        //good_ = false;
//        //return *this;
//        return pool;
//    }
//
//    pool = csdb::Pool::from_lz4_byte_stream(reinterpret_cast<const char*>(istream_.getCurrPtr()),
//        compressedSize, uncompressedSize);
//    //ptr_ += compressedSize;
//    istream_.safeSkip<uint8_t>(compressedSize);
//    return pool;
//}

void Node::passBlockToSolver(csdb::Pool& pool, const cs::PublicKey& sender)
{
    solver_->rndStorageProcessing();
    if(pool.sequence() == getBlockChain().getLastWrittenSequence() + 1) {
        if(getBlockChain().getLastHash() == pool.previous_hash()) {
            solver_->gotBlock(std::move(pool), sender);
        }
        else {
            size_t localSeq = getBlockChain().getLastWrittenSequence();
            size_t blockSeq = pool.sequence();
            cswarning() <<"Node: prev. hash of block [" << blockSeq << "] != blockchain last hash of block [" << localSeq << "]";
            cslog() <<"Blockchain last hash = " << getBlockChain().getLastHash().to_string();
            cslog() <<"Block prev. hash = " << pool.previous_hash().to_string();
            //TODO:: reimplement required
            //getBlockChain().revertLastBlock();
            solver_->gotIncorrectBlock(std::move(pool), sender);
        }
    }
    else {
        solver_->gotIncorrectBlock(std::move(pool), sender);
    }
}
//          A                                          A                                              A
//         / \                                        / \                                            / \
//         | |                                        | |                                            | |
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////                                              SOLVER 3 METHODS (FINISH)                                    ////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////