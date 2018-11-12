#include <sstream>
#include <numeric>
#include <algorithm>

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

#include <lz4.h>
#include <sodium.h>

#include <snappy.h>
#include <sodium.h>

const unsigned MIN_CONFIDANTS = 3;
const unsigned MAX_CONFIDANTS = 100;

const csdb::Address Node::genesisAddress_ = csdb::Address::from_string("0000000000000000000000000000000000000000000000000000000000000001");
const csdb::Address Node::startAddress_   = csdb::Address::from_string("0000000000000000000000000000000000000000000000000000000000000002");
const csdb::Address Node::spammerAddress_ = csdb::Address::from_string("0000000000000000000000000000000000000000000000000000000000000003");

Node::Node(const Config& config):
  nodeIdKey_(config.getMyPublicKey()),
  bc_(config.getPathToDB().c_str(), genesisAddress_, startAddress_, spammerAddress_),
  solver_(new slv2::SolverCore(this, genesisAddress_, startAddress_
#ifdef SPAMMER
    , spammerAddress_
#endif
  )),
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

  solver_->runSpammer();

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
  if (!isSyncroStarted_) {
    if (roundNum_ != getBlockChain().getLastWrittenSequence() + 1) {
      isSyncroStarted_ = true;
      roundToSync_ = roundNum_;

      cslog() << "Processing pools sync....";
      sendBlockRequest();
    }
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
  cs::Timer::singleShot(TIME_TO_AWAIT_SS_ROUND, [this]() {
    solver_->gotRound();
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
  if (myLevel_ != NodeLevel::Writer) {
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

  solver_->gotRound();
}

void Node::getCharacteristic(const uint8_t* data, const size_t size, const cs::RoundNumber round, const cs::PublicKey& sender) {
  cslog() << "NODE> Characteric has arrived";
  cs::Conveyer& conveyer = cs::Conveyer::instance();

  if (!conveyer.isSyncCompleted(round)) {
    cslog() << "NODE> Packet sync not finished, saving characteristic meta to call after sync";

    cs::Bytes characteristicBytes;
    characteristicBytes.assign(data, data + size);

    cs::CharacteristicMetaStorage::MetaElement metaElement;
    metaElement.meta.bytes = std::move(characteristicBytes);
    metaElement.meta.sender = sender;
    metaElement.round = conveyer.currentRoundNumber();

    conveyer.addCharacteristicMeta(std::move(metaElement));
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

  assert(sequence <= this->getRoundNumber());

  cs::PublicKey writerPublicKey;
  istream_ >> writerPublicKey;

  conveyer.setCharacteristic(characteristic);
  std::optional<csdb::Pool> pool = conveyer.applyCharacteristic(poolMetaInfo, writerPublicKey);

  if (isSyncroStarted_) {
    if (pool) {
      cs::PoolSyncMeta meta;
      meta.sender = sender;
      meta.signature = signature;
      meta.pool = std::move(pool).value();

      addPoolMetaToMap(std::move(meta), sequence);
    }

    return;
  }

  if (pool) {
    solver_->countFeesInPool(&pool.value());
    pool.value().set_previous_hash(bc_.getLastWrittenHash());
    getBlockChain().finishNewBlock(pool.value());

    if (pool.value().verify_signature(std::string(signature.begin(), signature.end()))) {
      cswarning() << "NODE> RECEIVED KEY Writer verification successfull";
      writeBlock(pool.value(), sequence, sender);
    }
    else {
      cswarning() << "NODE> RECEIVED KEY Writer verification failed";
      cswarning() << "NODE> remove wallets from wallets cache";
      getBlockChain().removeWalletsInPoolFromCache(pool.value());
    }
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
  if (cs::Conveyer::instance().isSyncCompleted()) {
    return;
  }

  const auto msgType = MsgTypes::TransactionsPacketRequest;
  const auto neighboursCount = transport_->getMaxNeighbours();
  const auto hashesCount = hashes.size();
  const bool isHashesLess = hashesCount < neighboursCount;
  const std::size_t remainderHashes = isHashesLess ? 0 : hashesCount % neighboursCount;
  const std::size_t amountHashesOfRequest = isHashesLess ? hashesCount : (hashesCount / neighboursCount);

  auto getRequestBytesClosure = [hashes](const std::size_t startHashNumber, std::size_t hashesCount) {
    cs::Bytes bytes;
    auto begin = reinterpret_cast<cs::Byte*>(&hashesCount);
    auto end = begin + sizeof(hashesCount);
    std::copy(begin, end, std::back_inserter(bytes));

    csdebug() << "NODE> Sending transaction packet request to Random Neighbour: hashes Count: " << hashesCount;

    for (auto i = 0; i < hashesCount; ++i) {
      auto& binary = hashes.at(startHashNumber + i).toBinary();
      std::copy(binary.begin(), binary.end(), std::back_inserter(bytes));
    }

    return bytes;
  };

  bool successRequest = false;

  for (std::size_t i = 0; i < neighboursCount; ++i) {
    const std::size_t count = i == (neighboursCount - 1) ? amountHashesOfRequest + remainderHashes : amountHashesOfRequest;

    successRequest = sendToRandomNeighbour(msgType, round, getRequestBytesClosure(i * amountHashesOfRequest, count));

    if (!successRequest) {
      cswarning() << "NODE> Sending transaction packet request: Cannot get a connection with a random neighbour";
      break;
    }

    if (isHashesLess) {
      break;
    }
  }

  if (!successRequest) {
    sendBroadcast(msgType, round, getRequestBytesClosure(0, hashesCount));
    return;
  }

  cs::Timer::singleShot(cs::PacketHashesRequestDelay, [round, this] {
                          const cs::Conveyer& conveyer = cs::Conveyer::instance();
                          if (!conveyer.isSyncCompleted()) {
                            sendPacketHashesRequestToRandomNeighbour(conveyer.currentNeededHashes(), round);
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

  sendBlockRequestSequence_ = sequence;
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

  if (pool.sequence() == sendBlockRequestSequence_) {
    cslog() << "GET BLOCK REPLY> Block Sequence is Ok";

    Node::showSyncronizationProgress(getBlockChain().getLastWrittenSequence(), roundToSync_);

    if (pool.sequence() == bc_.getLastWrittenSequence() + 1) {
      bc_.onBlockReceived(pool);
    }

    isAwaitingSyncroBlock_ = false;
  }
  else {
    cswarning() << "GET STRANGE SYNC POOL";
  }

  if (roundToSync_ != bc_.getLastWrittenSequence()) {
    sendBlockRequest();
  } else {
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
    solver_->gotRound();

    if (auto meta = conveyer.characteristicMeta(round); meta.has_value()) {
      csdebug() << "NODE> Run characteristic meta";
      getCharacteristic(meta->bytes.data(), meta->bytes.size(), round, meta->sender);
    }
  }
}

void Node::processTransactionsPacket(cs::TransactionsPacket&& packet) {
  cs::Conveyer::instance().addTransactionsPacket(packet);
}

void Node::onRoundStartConveyer(cs::RoundTable&& roundTable) {
  cs::Conveyer& conveyer = cs::Conveyer::instance();
  conveyer.setRound(std::move(roundTable));

  if (conveyer.isSyncCompleted()) {
    cslog() << "NODE> All hashes in conveyer";
  }
  else {
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
  if (!globalSequence) {
    return;
  }

  auto last = float(lastWrittenSequence);
  auto global = float(globalSequence);
  const float maxValue = 100.0f;
  const uint32_t syncStatus = cs::numeric_cast<uint32_t>((1.0f - (global - last) / global) * maxValue);

  if (syncStatus <= maxValue) {
    std::stringstream progress;
    progress << "SYNC: [";

    for (uint32_t i = 0; i < syncStatus; ++i) {
      if (i % 2) {
        progress << "#";
      }
    }

    for (uint32_t i = syncStatus; i < maxValue; ++i) {
      if (i % 2) {
        progress << "-";
      }
    }

    progress << "] " << syncStatus << "%";
    cslog() << progress.str();
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
