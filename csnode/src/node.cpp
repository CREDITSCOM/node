#include <sstream>
#include <numeric>
#include <algorithm>

#include <solver2/SolverCore.h>

#include <csnode/nodecore.h>
#include <csnode/node.hpp>
#include <csnode/conveyer.hpp>

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
  myPublicKey_(config.getMyPublicKey()),
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
  ostream_(&packStreamAllocator_, myPublicKey_) {
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

void Node::processPoolSync() {
  cslog() << "NODE> Processing pool syncronization algorithm";

  const auto currentSequence = bc_.getLastWrittenSequence();
  const auto needSequence = roundToSync_;
  const std::size_t mainCount = 1;
  const auto nodeSyncCount = mainCount + cs::Conveyer::instance().roundTable().confidants.size();
  const auto neededPoolCounts = needSequence - currentSequence;

  std::vector<csdb::Pool::sequence_t> sequences(neededPoolCounts, 0);
  std::iota(sequences.begin(), sequences.end(), currentSequence + 1);

  // choose strategy
  if (neededPoolCounts <= maxPoolCountToSync_) {
    sendBlockRequest(sequences);
  }
  else {
    auto splited = cs::Utils::splitVector(sequences, nodeSyncCount);

    for (const auto& part : splited) {
      std::size_t size = (part.size() < maxPoolCountToSync_) ? part.size() : maxPoolCountToSync_;

      sendBlockRequest(std::vector<csdb::Pool::sequence_t>(part.data(), part.data() + size));
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

  cs::Bytes bytes;
  cs::DataStream stream(bytes);

  stream << roundTable.round;
  stream << roundTable.confidants.size();
  stream << roundTable.hashes.size();
  stream << roundTable.general;

  for (const auto& confidant : roundTable.confidants) {
    stream << confidant;
    cslog() << __FUNCTION__ << " confidant: " << cs::Utils::byteStreamToHex(confidant.data(), confidant.size());
  }

  for (const auto& hash : roundTable.hashes) {
    stream << hash;
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

  ostream_ << bytes;

  flushCurrentTasks();
}

template <class... Args>
bool Node::sendNeighbours(const cs::PublicKey& target, const MsgTypes& msgType, const cs::RoundNumber round, const Args&... args) {
  cs::Bytes bytes;
  cs::DataStream stream(bytes);

  writeDefaultStream(stream, args...);

  return sendNeighbours(target, msgType, round, bytes);
}

bool Node::sendNeighbours(const cs::PublicKey& target, const MsgTypes& msgType, const cs::RoundNumber round, const cs::Bytes& bytes) {
  ConnectionPtr connection = transport_->getConnectionByKey(target);

  if (connection) {
    sendNeighbours(connection, msgType, round, bytes);
  }

  return connection;
}

void Node::sendNeighbours(const ConnectionPtr& target, const MsgTypes& msgType, const cs::RoundNumber round, const cs::Bytes& bytes) {
  ostream_.init(BaseFlags::Neighbours | BaseFlags::Broadcast | BaseFlags::Fragmented | BaseFlags::Compressed);
  ostream_ << msgType << round << bytes;

  csdebug() << "NODE> Sending data Direct: data size " << bytes.size();
  csdebug() << "NODE> Sending data Direct: to " << target->getOut();

  transport_->deliverDirect(ostream_.getPackets(), ostream_.getPacketsCount(), target);
  ostream_.clear();
}

template <class... Args>
void Node::sendBroadcast(const MsgTypes& msgType, const cs::RoundNumber round, const Args&... args) {
  cs::Bytes bytes;
  cs::DataStream stream(bytes);

  writeDefaultStream(stream, args...);

  sendBroadcast(msgType, round, bytes);
}

void Node::sendBroadcast(const MsgTypes& msgType, const cs::RoundNumber round, const cs::Bytes& bytes) {
  ostream_.init(BaseFlags::Broadcast | BaseFlags::Fragmented | BaseFlags::Compressed);
  csdebug() << "NODE> Sending broadcast";

  sendBroadcastImpl(msgType, round, bytes);
}

template <class... Args>
void Node::tryToSendDirect(const cs::PublicKey& target, const MsgTypes& msgType, const cs::RoundNumber round, const Args&... args) {
  cs::Bytes bytes;
  cs::DataStream stream(bytes);

  writeDefaultStream(stream, args...);

  tryToSendDirect(target, msgType, round, bytes);
}

void Node::tryToSendDirect(const cs::PublicKey& target, const MsgTypes& msgType, const cs::RoundNumber round, const cs::Bytes& bytes) {
  const bool success = sendNeighbours(target, msgType, round, bytes);

  if (!success) {
    sendBroadcast(target, msgType, round, bytes);
  }
}

template <class... Args>
bool Node::sendToRandomNeighbour(const MsgTypes& msgType, const cs::RoundNumber round, const Args&... args) {
  cs::Bytes bytes;
  cs::DataStream stream(bytes);

  writeDefaultStream(stream, args...);

  return sendToRandomNeighbour(msgType, round, bytes);
}

bool Node::sendToRandomNeighbour(const MsgTypes& msgType, const cs::RoundNumber round, const cs::Bytes& bytes) {
  ConnectionPtr target = transport_->getRandomNeighbour();

  if (target) {
    sendNeighbours(target, msgType, round, bytes);
  }

  return target;
}

void Node::getVector(const uint8_t* data, const size_t size, const cs::PublicKey& sender) {
  if (myLevel_ != NodeLevel::Confidant) {
    return;
  }

  if (myPublicKey_ == sender) {
    return;
  }

  cslog() << "NODE> Getting vector from " << cs::Utils::byteStreamToHex(sender.data(), sender.size());

  cs::DataStream stream(data, size);

  cs::HashVector vec;
  stream >> vec;

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

  if (myPublicKey_ == sender) {
    return;
  }

  cs::DataStream stream(data, size);

  cs::HashMatrix mat;
  stream >> mat;

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
  if (myLevel_ != NodeLevel::Confidant) {
    return;
  }

  cslog() << "Get hash size: " << size;

  cs::DataStream stream(data, size);

  csdb::PoolHash poolHash;
  stream >> poolHash;

  if (!istream_.good() || !istream_.end()) {
    cswarning() << "Bad hash packet format";
    return;
  }

  solver_->gotHash(std::move(poolHash), sender);
}

void Node::getTransactionsPacket(const uint8_t* data, const std::size_t size) {
  istream_.init(data, size);

  cs::Bytes bytes;
  istream_ >> bytes;

  cs::TransactionsPacket packet = cs::TransactionsPacket::fromBinary(bytes);

  if (packet.hash().isEmpty()) {
    cswarning() << "Received transaction packet hash is empty";
    return;
  }

  processTransactionsPacket(std::move(packet));
}

void Node::getPacketHashesRequest(const uint8_t* data, const std::size_t size, const cs::RoundNumber round, const cs::PublicKey& sender) {
  cs::DataStream stream(data, size);

  std::size_t hashesCount = 0;
  stream >> hashesCount;

  csdebug() << "NODE> Get packet hashes request: sender " << cs::Utils::byteStreamToHex(sender.data(), sender.size());

  cs::Hashes hashes;
  hashes.reserve(hashesCount);

  for (std::size_t i = 0; i < hashesCount; ++i) {
    cs::TransactionsPacketHash hash;
    stream >> hash;

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

  cs::DataStream stream(data, size);

  std::size_t packetsCount = 0;
  stream >> packetsCount;

  cs::Packets packets;
  packets.reserve(packetsCount);

  for (std::size_t i = 0; i < packetsCount; ++i) {
    cs::TransactionsPacket packet;
    stream >> packet;

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

  cs::DataStream stream(data, size);

  std::size_t confidantsCount = 0;
  stream >> confidantsCount;

  if (confidantsCount == 0) {
    cserror() << "Bad confidants count in round table";
    return;
  }

  std::size_t hashesCount = 0;
  stream >> hashesCount;

  cs::RoundTable roundTable;
  roundTable.round = round;

  // to node
  roundNum_ = round;

  cs::PublicKey general;
  stream >> general;

  cs::ConfidantsKeys confidants;
  confidants.reserve(confidantsCount);

  for (std::size_t i = 0; i < confidantsCount; ++i) {
    cs::PublicKey key;
    stream >> key;

    confidants.push_back(std::move(key));
  }

  cs::Hashes hashes;
  hashes.reserve(hashesCount);

  for (std::size_t i = 0; i < hashesCount; ++i) {
    cs::TransactionsPacketHash hash;
    stream >> hash;

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

  cs::DataStream stream(data, size);

  std::string time;
  cs::Bytes characteristicMask;
  uint64_t sequence = 0;

  cslog() << "NODE> Characteristic data size: " << size;

  stream >> time;
  stream >> characteristicMask >> sequence;

  cs::PoolMetaInfo poolMetaInfo;
  poolMetaInfo.sequenceNumber = sequence;
  poolMetaInfo.timestamp = std::move(time);

  cs::Signature signature;
  stream >> signature;

  std::size_t notificationsSize;
  stream >> notificationsSize;

  if (notificationsSize == 0) {
    cserror() << "NODE> Get characteristic: notifications count is zero";
  }

  for (std::size_t i = 0; i < notificationsSize; ++i) {
    cs::Bytes notification;
    stream >> notification;

    conveyer.addNotification(notification);
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
  stream >> writerPublicKey;

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
  if (!isCorrectNotification(data, size)) {
    cswarning() << "NODE> Notification failed " << cs::Utils::byteStreamToHex(sender.data(), sender.size());
    return;
  }

  cs::Conveyer& conveyer = cs::Conveyer::instance();
  cs::Bytes notification(data, data + size);
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

  ostream_.init(BaseFlags::Broadcast | BaseFlags::Compressed | BaseFlags::Fragmented);
  ostream_ << MsgTypes::NewCharacteristic << roundNum_;
  ostream_ << createBlockValidatingPacket(poolMetaInfo, conveyer.characteristic(), poolSignature, conveyer.notifications());
  ostream_ << solver_->getPublicKey();

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

  if (writerPublicKey != myPublicKey_) {
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

cs::Bytes Node::createBlockValidatingPacket(const cs::PoolMetaInfo& poolMetaInfo,
                                            const cs::Characteristic& characteristic,
                                            const cs::Signature& signature,
                                            const cs::Notifications& notifications) {
  cs::Bytes bytes;
  cs::DataStream stream(bytes);

  stream << poolMetaInfo.timestamp;
  stream << characteristic.mask;
  stream << poolMetaInfo.sequenceNumber;

  stream << signature;

  stream << notifications.size();

  for (const auto& notification : notifications) {
    stream << notification;
  }

  return bytes;
}

void Node::sendWriterNotification() {
  ostream_.init(BaseFlags::Compressed | BaseFlags::Fragmented, solver_->getWriterPublicKey());
  ostream_ << MsgTypes::WriterNotification;
  ostream_ << roundNum_;

  ostream_ << createNotification();

  cslog() << "NODE> Notification sent to writer";

  flushCurrentTasks();
}

cs::Bytes Node::createNotification() {
  cs::Hash characteristicHash = cs::Conveyer::instance().characteristicHash();
  cs::PublicKey writerPublicKey = solver_->getWriterPublicKey();

  cs::Bytes bytes;
  cs::DataStream stream(bytes);

  stream << characteristicHash << writerPublicKey;

  cs::Signature signature = cs::Utils::sign(bytes, solver_->getPrivateKey());

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

  cs::Bytes bytes;
  cs::DataStream stream(bytes);

  stream << hash;

  ostream_.init(BaseFlags::Fragmented, target);
  ostream_ << MsgTypes::BlockHash << roundNum_ << bytes;

  flushCurrentTasks();
}

void Node::sendTransactionsPacket(const cs::TransactionsPacket& packet) {
  if (packet.hash().isEmpty()) {
    cswarning() << "Send transaction packet with empty hash failed";
    return;
  }

  ostream_.init(BaseFlags::Compressed | BaseFlags::Fragmented | BaseFlags::Broadcast);
  ostream_ << MsgTypes::TransactionPacket << roundNum_ << packet.toBinary();

  flushCurrentTasks();
}

void Node::sendPacketHashesRequest(const cs::Hashes& hashes, const cs::RoundNumber round) {
  if (myLevel_ == NodeLevel::Writer) {
    cserror() << "Writer should has all transactions hashes";
    return;
  }

  cs::Bytes bytes;
  cs::DataStream stream(bytes);

  const std::size_t hashesSize = hashes.size();

  stream << hashesSize;
  csdebug() << "NODE> Sending packet hashes request: " << hashesSize;

  for (const auto& hash : hashes) {
    stream << hash;
  }

  const auto msgType = MsgTypes::TransactionsPacketRequest;

  const auto& general = cs::Conveyer::instance().roundTable().general;
  const bool sendToGeneral = sendNeighbours(general, msgType, round, bytes);

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

  auto getRequestBytesClosure = [hashes](const std::size_t startHashNumber, const std::size_t hashesCount) {
    cs::Bytes bytes;
    cs::DataStream stream(bytes);

    stream << hashesCount;

    csdebug() << "NODE> Sending transaction packet request to Random Neighbour: hashes Count: " << hashesCount;

    for (auto i = 0; i < hashesCount; ++i) {
      stream << hashes.at(startHashNumber + i);
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

  cs::Bytes bytes;
  cs::DataStream stream(bytes);

  stream << packets.size();
  csdebug() << "NODE> Sending transaction packet reply: packets count: " << packets.size();

  for (const auto& packet : packets) {
    stream << packet;
  }

  const auto msgType = MsgTypes::TransactionsPacketReply;
  const bool success = sendNeighbours(target, msgType, round, bytes);

  if (!success) {
    csdebug() << "NODE> Sending transaction packet reply: Cannot get a connection with a specified public key " << cs::Utils::byteStreamToHex(target.data(), target.size());

    sendBroadcast(target, msgType, round, bytes);
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

void Node::sendBlockRequest(const std::vector<csdb::Pool::sequence_t>& sequences) {
  const auto& roundTable = cs::Conveyer::instance().roundTable();

  // create destinations
  std::vector<cs::PublicKey> keys;
  keys.push_back(roundTable.general);
  keys.insert(keys.end(), roundTable.confidants.begin(), roundTable.confidants.end());

  const auto maxTries = 10;
  const auto msgType = MsgTypes::BlockRequest;

  cs::Bytes bytes;
  cs::DataStream stream(bytes);

  stream << sequences.size();

  for (auto& seq : sequences) {
    stream << seq;
  }

  // random confidant search
  for (std::size_t i = 0; i < maxTries; ++i) {
    std::size_t randomIndex = static_cast<std::size_t>(cs::Utils::generateRandomValue(0, static_cast<int>(keys.size() - 1)));
    ConnectionPtr connection = transport_->getConnectionByKey(keys[randomIndex]);

    if (connection) {
      sendNeighbours(connection, msgType, roundNum_, bytes);
      csdebug() << "SEND BLOCK REQUEST> Sending request for block: " << sequences.size();
      return;
    }
  }

  std::size_t randomIndex = static_cast<std::size_t>(cs::Utils::generateRandomValue(0, static_cast<int>(keys.size() - 1)));

  sendBroadcast(keys[randomIndex], msgType, roundNum_, bytes);

  csdebug() << "SEND BLOCK REQUEST> Sending request for block: " << sequences.size();
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
    cslog() << "SYNCRO FINISHED!!!";
  }
}

void Node::sendBlockReply(const csdb::Pool& pool, const cs::PublicKey& target) {
  ConnectionPtr conn = transport_->getConnectionByKey(target);
  if (!conn) {
    cswarning() << "Cannot get a connection with a specified public key";
    return;
  }

  ostream_.init(BaseFlags::Neighbours | BaseFlags::Broadcast | BaseFlags::Fragmented | BaseFlags::Compressed);
  ostream_ << MsgTypes::RequestedBlock << roundNum_ << pool;

  transport_->deliverDirect(ostream_.getPackets(),
                            ostream_.getPacketsCount(),
                            conn);
  ostream_.clear();
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
  cslog() << "Node PK = " << cs::Utils::byteStreamToHex(myPublicKey_.data(), myPublicKey_.size());

  const cs::ConfidantsKeys& confidants = roundTable.confidants;

  if (roundTable.general == myPublicKey_) {
    myLevel_ = NodeLevel::Main;
  }
  else {
    const auto iter = std::find(confidants.begin(), confidants.end(), myPublicKey_);

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

void Node::initNextRound(std::vector<cs::PublicKey>&& confidantNodes)
{
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

    initNextRound(table);
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

template <class T, class... Args>
void Node::writeDefaultStream(cs::DataStream& stream, const T& value, const Args&... args) {
  stream << value;
  writeDefaultStream(stream, args...);
}

template<class T>
void Node::writeDefaultStream(cs::DataStream& stream, const T& value) {
  stream << value;
}

void Node::sendBroadcast(const cs::PublicKey& target, const MsgTypes& msgType, const cs::RoundNumber round, const cs::Bytes& bytes) {
  ostream_.init(BaseFlags::Fragmented | BaseFlags::Compressed, target);
  csdebug() << "NODE> Sending broadcast to: " << cs::Utils::byteStreamToHex(target.data(), target.size());

  sendBroadcastImpl(msgType, round, bytes);
}

void Node::sendBroadcastImpl(const MsgTypes& msgType, const cs::RoundNumber round, const cs::Bytes& bytes) {
  ostream_ << msgType << round << bytes;

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
    LOG_WARN("Only confidant nodes can send consensus stages");
    return;
  }
  LOG_DEBUG( "Round = " << roundNum_
    << " Sender: " << (int)stageOneInfo.sender << std::endl
    << " Hash: " << cs::Utils::byteStreamToHex(stageOneInfo.hash.data(), stageOneInfo.hash.size())
    << " Cand Amount: " << (int)stageOneInfo.candidatesAmount);
    

  
  size_t msgSize = 34 + 32 * stageOneInfo.candidatesAmount;
  auto memPtr = allocator_.allocateNext(msgSize);
  uint8_t* rawData = (uint8_t*)memPtr.get();

  *rawData = stageOneInfo.sender;
  memcpy(rawData + 1, stageOneInfo.hash.data(), stageOneInfo.hash.size());
  *(rawData + 33) = stageOneInfo.candidatesAmount;
  for (int i = 0; i < stageOneInfo.candidatesAmount; i++) {
    memcpy(rawData + 34 + 32 * i, stageOneInfo.candiates[i].data(), stageOneInfo.candiates[i].size());
  }
  //std::cout << "Sent message: (" << msgSize << ") : " << byteStreamToHex((const char*)rawData, msgSize) << std::endl;
  
  unsigned long long sig_size;
  crypto_sign_ed25519_detached(stageOneInfo.sig.data(), &sig_size, rawData, msgSize, solver_->getPrivateKey().data());
  //std::cout << " Sig: " << byteStreamToHex((const char*)stageOneInfo.sig.val, 64) << std::endl;
  ostream_.init(BaseFlags::Broadcast);
  ostream_ << MsgTypes::FirstStage
    << roundNum_
    << msgSize
    << stageOneInfo.sig;
  ostream_ << std::string(cs::numeric_cast<const char*>((void*)rawData), msgSize);
  allocator_.shrinkLast(msgSize);
  LOG_DEBUG("done");
  flushCurrentTasks();
}


// sends StageOne request to respondent about required
void Node::requestStageOne(uint8_t respondent, uint8_t required) {
#ifdef MYLOG
  std::cout << "NODE> Stage ONE requesting ... ";
#endif
  if (myLevel_ != NodeLevel::Confidant) {
    LOG_WARN("Only confidant nodes can request consensus stages");
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
  //std::cout << "NODE> Getting StageOne Request" << std::endl;
  if (myLevel_ != NodeLevel::Confidant) {
    return;
}
  // std::cout << "NODE> Getting StageOne 0" << std::endl;
  if (myPublicKey_ == requester) {
    return;
  }
  //std::cout << "NODE> Getting StageOne 1" << std::endl;
  //LOG_EVENT(FILE_NAME_ << "Getting Stage One Request from " << byteStreamToHex(sender.str, 32));
  istream_.init(data, size);
  uint8_t requesterNumber;
  uint8_t requiredNumber;
  istream_ >> requesterNumber >> requiredNumber;

  if(requester != cs::Conveyer::instance().roundTable().confidants.at(requesterNumber)) {
      return;
  }
  if (!istream_.good() || !istream_.end()) {
    LOG_ERROR("Bad StageOne packet format");
    return;
  }
  solver_->gotStageOneRequest(requesterNumber, requiredNumber);
}

void Node::sendStageOneReply(const cs::StageOne& stageOneInfo, const uint8_t requester) {
#ifdef MYLOG
  std::cout << "NODE> Stage ONE Reply sending ... ";
#endif
  if (myLevel_ != NodeLevel::Confidant) {
    LOG_WARN("Only confidant nodes can send consensus stages");
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
    //std::cout << i << ". " << byteStreamToHex(stageOneInfo.candiates[i].str, 32) << std::endl;
  }

  ostream_.init(0/*need no flags!*/, cs::Conveyer::instance().roundTable().confidants.at(requester));

  ostream_ << MsgTypes::FirstStage
    << roundNum_
    << msgSize
    << stageOneInfo.sig;
  ostream_ << std::string(cs::numeric_cast<const char *>((void*) rawData), msgSize);
  LOG_DEBUG("Round = " << roundNum_
    << " Sender: " << (int)stageOneInfo.sender << std::endl
    << " Hash: " << cs::Utils::byteStreamToHex(stageOneInfo.hash.data(), stageOneInfo.hash.size())
    << " Cand Amount: " << (int)(stageOneInfo.candidatesAmount)
    << " Sig: " << cs::Utils::byteStreamToHex(stageOneInfo.sig.data(), stageOneInfo.sig.size()));

  for (int i = 0; i < stageOneInfo.candidatesAmount; i++) {
    ostream_ << stageOneInfo.candiates[i];
    LOG_DEBUG(i << ". " << cs::Utils::byteStreamToHex(stageOneInfo.candiates[i].data(), stageOneInfo.candiates[i].size()));
  }
  flushCurrentTasks();
  allocator_.shrinkLast(msgSize);
  LOG_DEBUG("done");
}

void Node::getStageOne(const uint8_t* data, const size_t size, const cs::PublicKey& sender) {
  LOG_DEBUG(__func__);
  //std::cout << "NODE> Getting StageOne" << std::endl;
  if (myLevel_ != NodeLevel::Confidant) {
    return;
  }
 // std::cout << "NODE> Getting StageOne 0" << std::endl;
  if (myPublicKey_ == sender) {
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
      LOG_ERROR("Bad StageOne packet format");
      return;
  }

  const uint8_t* stagePtr = (const uint8_t*) raw_bytes.data();

  auto memPtr = allocator_.allocateNext(msgSize);
  uint8_t* rawData = (uint8_t*)memPtr.get();

  memcpy(rawData, stagePtr, msgSize);

   //std::cout << "Received message: "<< byteStreamToHex((const char*)stagePtr , msgSize) << std::endl;

  stage.sender = *rawData;
  if (crypto_sign_ed25519_verify_detached(stage.sig.data(), rawData,
    msgSize, (const unsigned char*) cs::Conveyer::instance().roundTable().confidants.at(stage.sender).data())) {
    std::cout << "NODE> Stage One from [" << (int)stage.sender << "] -  WRONG SIGNATURE!!!" << std::endl;
      return;
  }
  //std::cout << "NODE> Signature is OK!" << std::endl;
  memcpy(stage.hash.data(), rawData + 1, 32);
  stage.candidatesAmount = *(rawData + 33);
  for (int i = 0; i < stage.candidatesAmount; i++) {
    memcpy(stage.candiates[i].data(), rawData + 34 + 32 * i, 32);
    std::cout << i << ". " << cs::Utils::byteStreamToHex(stage.candiates[i].data(), stage.candiates[i].size()) << std::endl;
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
  std::cout << "NODE> +++++++++++++++++++++++ Stage TWO sending +++++++++++++++++++++++++++" << std::endl;
#endif
  if ((myLevel_ != NodeLevel::Confidant) && (myLevel_ != NodeLevel::Writer)) {
    LOG_WARN("Only confidant nodes can send consensus stages");
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
  //std::cout << "Sent message: (" << msgSize << ") : " << byteStreamToHex((const char*)rawData, msgSize) << std::endl;

  unsigned long long sig_size;
  crypto_sign_ed25519_detached((unsigned char*)stageTwoInfo.sig.data(), &sig_size, rawData, msgSize, solver_->getPrivateKey().data());

  ostream_.init(BaseFlags::Broadcast);
  ostream_ << MsgTypes::SecondStage
    << roundNum_
    << msgSize
    << stageTwoInfo.sig;
  ostream_ << std::string(cs::numeric_cast<const char*>((void*) rawData), msgSize);
  
 // std::cout << "NODE> Sending StageTwo:" << std::endl;
  //for (int i = 0; i < stageTwoInfo.trustedAmount; i++) {
  //  std::cout << " Sig[" << i << "]: " << byteStreamToHex((const char*)stageTwoInfo.signatures[i].val, 64) << std::endl;
  //}
  allocator_.shrinkLast(msgSize);
  LOG_DEBUG("done");
  flushCurrentTasks();
}

void Node::requestStageTwo(uint8_t respondent, uint8_t required)
{
  if ((myLevel_ != NodeLevel::Confidant) && (myLevel_ != NodeLevel::Writer)) {
    LOG_WARN("Only confidant nodes can request consensus stages");
    return;
  }
  //#ifdef MYLOG
  std::cout << "==============================" << std::endl;
  std::cout << "NODE> Stage TWO requesting ... " << std::endl;
  std::cout << "==============================" << std::endl;
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
  if (myPublicKey_ == requester) {
    return;
  }
  //LOG_EVENT(FILE_NAME_ << "Getting Stage Two Request from " << byteStreamToHex(sender.str, 32));
  istream_.init(data, size);
  uint8_t requesterNumber;
  uint8_t requiredNumber;
  istream_ >> requesterNumber >> requiredNumber;

  std::cout << "NODE> Getting StageTwo Request from [" << (int)requesterNumber <<"] " << std::endl;
  if (requester != cs::Conveyer::instance().roundTable().confidants.at(requesterNumber)) return;

  if (!istream_.good() || !istream_.end()) {
    LOG_ERROR("Bad StageTwo packet format");
    return;
  }
  solver_->gotStageTwoRequest(requesterNumber, requiredNumber);
}

void Node::sendStageTwoReply(const cs::StageTwo& stageTwoInfo, const uint8_t requester)
{
//#ifdef MYLOG
  std::cout << "NODE> Stage Two REPLY sendiing" << std::endl;
//#endif
  if ((myLevel_ != NodeLevel::Confidant) && (myLevel_ != NodeLevel::Writer)) {
    LOG_WARN("Only confidant nodes can send consensus stages");
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
  //std::cout << "Sent message: (" << msgSize << ") : " << byteStreamToHex((const char*)rawData, msgSize) << std::endl;

  ostream_.init(0/*need no flags!*/, cs::Conveyer::instance().roundTable().confidants.at(requester));

  ostream_ << MsgTypes::SecondStage
    << roundNum_
    << msgSize
    << stageTwoInfo.sig;
  ostream_ << std::string(cs::numeric_cast<const char*>((void*) rawData), msgSize);
  //std::cout << "NODE> Sending StageTwo:" << std::endl;
  //for (int i = 0; i < stageTwoInfo.trustedAmount; i++) {
  //  std::cout << " Sig[" << i << "]: " << byteStreamToHex((const char*)stageTwoInfo.signatures[i].val, 64) << std::endl;
  //}
  allocator_.shrinkLast(msgSize);
  LOG_DEBUG("done");
  flushCurrentTasks();
}

void Node::getStageTwo(const uint8_t* data, const size_t size, const cs::PublicKey& sender)
{
    LOG_DEBUG(__func__);
    //std::cout << "NODE> Getting StageTwo" << std::endl;
  if ((myLevel_ != NodeLevel::Confidant) && (myLevel_ != NodeLevel::Writer)) {
    return;
  }
  if (myPublicKey_ == sender) {
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
      LOG_ERROR("Bad StageTwo packet format");
      return;
  }

  const uint8_t* stagePtr = (uint8_t*) raw_bytes.data();
  auto memPtr = allocator_.allocateNext(msgSize);
  uint8_t* rawData = (uint8_t*)memPtr.get();
  memcpy(rawData, stagePtr, msgSize);

  stage.sender = *rawData;
  //std::cout << "Received message: "<< byteStreamToHex((const char*)stagePtr, msgSize) << std::endl;
  if (crypto_sign_ed25519_verify_detached(stage.sig.data(), rawData,
    msgSize, (const unsigned char*) cs::Conveyer::instance().roundTable().confidants.at(stage.sender).data()))
  {
    std::cout << "NODE> Stage Two from ["<< (int)stage.sender << "] -  WRONG SIGNATURE!!!" <<std::endl;
    return;
  }
  
  //std::cout << "NODE> Stage Two [" << (int)stage.sender << "] signature is OK!" << std::endl;
 // std::cout << "Package Signature: " << byteStreamToHex((const char*)stage.sig.val, 64) << std::endl;
  stage.trustedAmount = *(rawData + 1);
  for (int i = 0; i < stage.trustedAmount; i++) {
    memcpy(stage.signatures[i].data(), rawData + 2 + 64 * i, 64);
    //std::cout << " Sig[" << i << "]: " << byteStreamToHex((const char*)stage.signatures[i].val, 64) << std::endl;
  }

  allocator_.shrinkLast(msgSize);
  solver_->gotStageTwo(std::move(stage));
}

void Node::sendStageThree(const cs::StageThree& stageThreeInfo)
{
    LOG_DEBUG(__func__);
#ifdef MYLOG
  std::cout << "NODE> Stage THREE sending" << std::endl;
#endif
  if (myLevel_ != NodeLevel::Confidant) {
    LOG_WARN("Only confidant nodes can send consensus stages");
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
  std::cout << "NODE> Stage THREE requesting ... ";
#endif
  if (myLevel_ != NodeLevel::Confidant) {
    LOG_WARN("Only confidant nodes can request consensus stages");
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
  //std::cout << "NODE> Getting StageThree Request" << std::endl;
  if (myLevel_ != NodeLevel::Confidant) {
    return;
  }
  if (myPublicKey_ == requester) {
    return;
  }
  //LOG_EVENT(FILE_NAME_ << "Getting Stage Three Request from " << byteStreamToHex(sender.str, 32));
  istream_.init(data, size);
  uint8_t requesterNumber;
  uint8_t requiredNumber;
  istream_ >> requesterNumber >> requiredNumber;

  if (requester != cs::Conveyer::instance().roundTable().confidants.at(requesterNumber)) return;

  if (!istream_.good() || !istream_.end()) {
    LOG_ERROR("Bad StageThree packet format");
    return;
  }
  solver_->gotStageThreeRequest(requesterNumber, requiredNumber);
}

void Node::sendStageThreeReply(const cs::StageThree& stageThreeInfo, const uint8_t requester)
{
  LOG_DEBUG(__func__);
#ifdef MYLOG
  std::cout << "NODE> Stage THREE Reply sending" << std::endl;
#endif
  if (myLevel_ != NodeLevel::Confidant) {
    LOG_WARN("Only confidant nodes can send consensus stages");
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
  if (myPublicKey_ == sender) {
    return;
  }
  //std::cout << "NODE> Getting Stage Three  " << std::endl;
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
    LOG_ERROR("Bad StageTwo packet format");
    return;
  }
  stage.sender = *rawData;

  //std::cout << "Received message: "<< byteStreamToHex((const char*)rawData, msgSize) << std::endl;
  if (crypto_sign_ed25519_verify_detached(stage.sig.data(), rawData,
    msgSize, (const unsigned char*)cs::Conveyer::instance().roundTable().confidants.at(stage.sender).data())) {
      std::cout << "NODE> Stage Three from ["<< (int)stage.sender << "] -  WRONG SIGNATURE!!!" <<std::endl;
    return;
  }
  //std::cout << "NODE> Signature is OK!" << std::endl;
  stage.writer = *(rawData +1);
  memcpy(stage.hashBlock.data(), rawData + 2 , 32);
  memcpy(stage.hashCandidatesList.data(), rawData + 34, 32);
  allocator_.shrinkLast(msgSize);
  solver_->gotStageThree(std::move(stage));
}


void Node::sendRoundInfo_(const cs::RoundTable& roundTable) {

  csdebug() << "NODE> Apply notifications";

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

  writeBlock(pool.value(), poolMetaInfo.sequenceNumber, cs::PublicKey());

  /////////////////////////////////////////////////////////////////////////// sending round info and block
  ostream_.init(BaseFlags::Broadcast | BaseFlags::Compressed | BaseFlags::Fragmented);
  ostream_ << MsgTypes::RoundInfo << roundTable.round;
  ostream_ << roundTable.confidants.size();
  ostream_ << roundTable.hashes.size();
  for (const auto& confidant : roundTable.confidants) {
    ostream_ << confidant;
    cslog() << __FUNCTION__ << " confidant: " << cs::Utils::byteStreamToHex(confidant.data(), confidant.size());
  }

  for (const auto& hash : roundTable.hashes) {
    ostream_ << hash;
  }
  ostream_ << solver_->getPublicKey();
  cs::Bytes charFunc = createBlockValidatingPacket(poolMetaInfo, conveyer.characteristic(), poolSignature, conveyer.notifications());

  ostream_ << charFunc.size()<< charFunc;


 /* cs::Bytes bytes;
  cs::DataStream stream(bytes);*/


  //stream << roundTable.general;
  //LOG_DEBUG(__func__);

  //if (myLevel_ != NodeLevel::Writer) {
  //  LOG_WARN("Only WRITER nodes can send ROUNDINFO");
  //  return;
  //}




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
  transport_->clearTasks();

  //TODO: обновить таблицу раунда в cs::Conveyer::instance()
  //cs::Conveyer::instance().roundTable().confidants.assign(confidantNodes.cbegin(), confidantNodes.cend());
  assert(false);

  onRoundStart_V3();

  //if (getNodeLevel() == NodeLevel::Confidant) {
  //  solver_->gotTransactionList_V3(std::move(tmpPool));
  //}
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/*void Node::getCharacteristic(const uint8_t* data, const size_t size, const cs::RoundNumber round, const cs::PublicKey& sender) {
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

  cs::DataStream stream(data, size);

  std::string time;
  cs::Bytes characteristicMask;
  uint64_t sequence = 0;

  cslog() << "NODE> Characteristic data size: " << size;

  stream >> time;
  stream >> characteristicMask >> sequence;

  cs::PoolMetaInfo poolMetaInfo;
  poolMetaInfo.sequenceNumber = sequence;
  poolMetaInfo.timestamp = std::move(time);

  cs::Signature signature;
  stream >> signature;

  std::size_t notificationsSize;
  stream >> notificationsSize;

  if (notificationsSize == 0) {
    cserror() << "NODE> Get characteristic: notifications count is zero";
  }

  for (std::size_t i = 0; i < notificationsSize; ++i) {
    cs::Bytes notification;
    stream >> notification;

    conveyer.addNotification(notification);
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
  stream >> writerPublicKey;

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
}*/
///////////////////////////////////////////////////////////////////////////////


void Node::getRoundInfo_(const uint8_t * data, const size_t size, const cs::RoundNumber rNum, const cs::PublicKey& sender) {
  LOG_DEBUG(__func__);
  if (myLevel_ == NodeLevel::Writer) {
    LOG_WARN("NODE> Writers don't need ROUNDINFO");
    return;
  }
  //LOG_EVENT(FILE_NAME_ << "Getting RoundInfo from " << byteStreamToHex(sender.str, 32));

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
  roundTable.round = rNum;

  // to node
  roundNum_ = rNum;

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

//  roundTable.general = std::move(general);
  roundTable.confidants = std::move(confidants);
  roundTable.hashes = std::move(hashes);


  ///////////////////////////////////// Round table received , parcing char func
  cs::PublicKey writerPublicKey;
  istream_ >> writerPublicKey;  

  cslog() << "NODE> Characteric has arrived";
  cs::Conveyer& conveyer = cs::Conveyer::instance();


  std::size_t charFuncSize = 0;
  istream_ >> charFuncSize;

  if (!conveyer.isSyncCompleted(rNum)) {
    cslog() << "NODE> Packet sync not finished, saving characteristic meta to call after sync";

    cs::Bytes characteristicBytes;
    characteristicBytes.assign(istream_.getCurrPtr(), istream_.getCurrPtr() + charFuncSize);

    cs::CharacteristicMetaStorage::MetaElement metaElement;
    metaElement.meta.bytes = std::move(characteristicBytes);
    metaElement.meta.sender = sender;
    metaElement.round = conveyer.currentRoundNumber();

    conveyer.addCharacteristicMeta(std::move(metaElement));
    return;
  }

  cs::DataStream stream((char*)istream_.getCurrPtr(), charFuncSize);

  std::string time;
  cs::Bytes characteristicMask;
  uint64_t sequence = 0;

  cslog() << "NODE> Characteristic data size: " << size;

  stream >> time;
  stream >> characteristicMask >> sequence;

  cs::PoolMetaInfo poolMetaInfo;
  poolMetaInfo.sequenceNumber = sequence;
  poolMetaInfo.timestamp = std::move(time);

  cs::Signature signature;
  stream >> signature;

  std::size_t notificationsSize;
  stream >> notificationsSize;

  if (notificationsSize == 0) {
    cserror() << "NODE> Get characteristic: notifications count is zero";
  }

  for (std::size_t i = 0; i < notificationsSize; ++i) {
    cs::Bytes notification;
    stream >> notification;

    conveyer.addNotification(notification);
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
  ////////////////////////////////////////////////////////////////////////////////////////////////////
  //onRoundStart(roundTable);
  //onRoundStartConveyer(std::move(roundTable));
  //solver_->gotRound();

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



  for (int i = 0; i < roundTable.confidants.size(); i++) {
    std::cout << i << ". " << cs::Utils::byteStreamToHex(roundTable.confidants.at(i).data(), confidants.at(i).size()) << std::endl;
  }
 /* uint8_t stageThreeNumber = 0;
  istream_ >> stageThreeNumber;
  std::vector<cs::StageThree> stageThreeIncoming;
  stageThreeIncoming.reserve(stageThreeNumber);
  cs::StageThree tempStage;

  for (int i = 0; i < stageThreeNumber; i++) {
    istream_ >> tempStage;
    stageThreeIncoming.push_back(tempStage);
  }*/
//#ifdef MYLOG
//  std::cout << "--------------------------------------------------------------------------------------" << std::endl;
//  std::cout << (int)stageThreeNumber << " : ";
//  for (int i = 0; i < stageThreeNumber; i++) {
//    std::cout << (int)stageThreeIncoming.at(i).sender;
//  }
//  std::cout << std::endl << "--------------------------------------------------------------------------------------" << std::endl;
//#endif
//
//  istream_ >> newPool >> poolToVerify;
//
//  if (!istream_.good()) {
//    LOG_ERROR("Node: packet with round info is corrupted");
//    //TODO: decide what to do
//  }
//#ifdef MYLOG
//  uint32_t bSize;
//  const char* bl = poolToVerify.to_byte_stream(bSize);
//  std::cout << "GET> PoolToVerify: " << poolToVerify.sequence() << ", tr_amount =" << poolToVerify.transactions_count() << ", " << ", " << byteStreamToHex(bl, bSize) << std::endl;
//  const char* b2 = newPool.to_byte_stream(bSize);
//  std::cout << "newPool: " << newPool.sequence() << ", " << byteStreamToHex(b2, bSize) << std::endl << std::endl;
//#endif
//  passBlockToSolver(newPool, sender);
//
//  getBlockChain().setGlobalSequence(newPool.sequence());
//  transport_->clearTasks();

  //TODO: обновить таблицу раунда в cs::Conveyer::instance()
  // std::swap(confidants, cs::Conveyer::instance().roundTable().confidants);
  /*assert(false);*/

 /* onRoundStart_V3();*/
  // let solver to decide what to do: if (getMyLevel() == NodeLevel::Confidant) {
  //solver_->gotTransactionList_V3(std::move(poolToVerify));
  //}
}

void Node::sendRoundInfo( const std::vector<cs::PublicKey>& confidantNodes, 
                          const csdb::Pool& poolToVerify, 
                          const csdb::Pool& newPool, 
                          const std::vector <cs::StageThree>& stageThreeStorage) {
    LOG_DEBUG(__func__);

  //if (myLevel_ != NodeLevel::Writer) {
  //  LOG_WARN("Only WRITER nodes can send ROUNDINFO");
  //  return;
  //}

#ifdef MYLOG
  std::cout << "NODE> ROUNDINFO sending" << std::endl;
#endif
#ifdef FLAG_LOG_DEBUG
  for (int i = 0; i < confidantNodes.size(); i++) {
      LOG_DEBUG(i << ". " << byteStreamToHex(confidantNodes.at(i).str, 32));
  }
#endif
#ifdef MYLOG
  std::cout << "------------------------------ Round Info --------------------------------------------" << std::endl;
  std::cout << (int)stageThreeStorage.size() << " : ";
  for (int i = 0; i < stageThreeStorage.size(); i++) {
    std::cout << (int)stageThreeStorage.at(i).sender;
  }
  std::cout << std::endl << "--------------------------------------------------------------------------------------" << std::endl;
#endif
  uint8_t confSize = (uint8_t)confidantNodes.size();
  LOG_DEBUG("Conf size = " << (int)confSize);
  ++roundNum_;
  ostream_.init(BaseFlags::Broadcast | BaseFlags::Fragmented);
  ostream_   << MsgTypes::RoundInfo
             << roundNum_
             << confSize;
 
  for (auto& iter : confidantNodes) {
    ostream_ << iter;
  }
  ostream_ << (uint8_t)stageThreeStorage.size();

  for (auto& iter : stageThreeStorage) {
    ostream_ << iter;
  }

  addCompressedPoolToPack(newPool);
  addCompressedPoolToPack(poolToVerify);

  //ostream_ << newPool << poolToVerify ;
  //composeMessageWithBlock1(newPool);
  //composeMessageWithBlock1(poolToVerify);

  csdb::Pool tmpNewPool(newPool);
  csdb::Pool tmpPool(poolToVerify);

#ifdef MYLOG
  uint32_t bSize;
  const char* bl =tmpPool.to_byte_stream(bSize);
  std::cout << "SEND> PoolToVerify: " << tmpPool.sequence()<< ", tr_amount =" << tmpPool.transactions_count() << ", " << byteStreamToHex(bl, bSize) << std::endl;
   const char* b2 = tmpNewPool.to_byte_stream(bSize);
  std::cout << "newPool: " << newPool.sequence() << ", " << byteStreamToHex(b2, bSize) << std::endl << std::endl;
#endif
  flushCurrentTasks();
  transport_->clearTasks();

  //TODO: обновить таблицу раунда в cs::Conveyer::instance()
  //cs::Conveyer::instance().roundTable().confidants.assign(confidantNodes.cbegin(), confidantNodes.cend());
  assert(false);

  onRoundStart_V3();

  if (getNodeLevel() == NodeLevel::Confidant) {
    solver_->gotTransactionList_V3(std::move(tmpPool));
  }
}

void Node::getRoundInfo(const uint8_t * data, const size_t size, const cs::RoundNumber roundNum,const cs::PublicKey& sender) {
  LOG_DEBUG(__func__);
  if (myLevel_ == NodeLevel::Writer) {
    LOG_WARN("NODE> Writers don't need ROUNDINFO");
    return;
  }
  //LOG_EVENT(FILE_NAME_ << "Getting RoundInfo from " << byteStreamToHex(sender.str, 32));
  
  uint8_t confSize = 0;
  int tempVar=0;
  csdb::Pool poolToVerify;
  csdb::Pool newPool;
  std::vector <cs::StageThree> stageThreeStorage;

  istream_.init(data, size);

  istream_ >> confSize;
 
  LOG_DEBUG("Conf size = " << (int)confSize);
  if (confSize < MIN_CONFIDANTS || confSize > MAX_CONFIDANTS) {
    LOG_WARN("Bad confidants number");
    return;
  }
  roundNum_ = roundNum;
  std::vector<cs::PublicKey> confidants;
  confidants.reserve(confSize);

  for (int i=0; i< confSize; i++) {
    confidants.push_back(cs::PublicKey());
    istream_ >> confidants.back();
  }

  for (int i = 0; i < confSize; i++) {
    std::cout << i << ". " << cs::Utils::byteStreamToHex(confidants.at(i).data(), confidants.at(i).size()) << std::endl;
  }
  uint8_t stageThreeNumber = 0;
  istream_ >> stageThreeNumber;
  std::vector<cs::StageThree> stageThreeIncoming;
  stageThreeIncoming.reserve(stageThreeNumber);
  cs::StageThree tempStage;

  for(int i = 0; i < stageThreeNumber; i++) {
      istream_ >> tempStage;
      stageThreeIncoming.push_back(tempStage);
}
#ifdef MYLOG
  std::cout << "--------------------------------------------------------------------------------------" << std::endl;
  std::cout << (int)stageThreeNumber << " : ";
  for (int i = 0; i < stageThreeNumber; i++) {
    std::cout << (int)stageThreeIncoming.at(i).sender;
  }
  std::cout << std::endl << "--------------------------------------------------------------------------------------" << std::endl;
#endif

  istream_ >> newPool >> poolToVerify;

  if(!istream_.good()) {
      LOG_ERROR("Node: packet with round info is corrupted");
      //TODO: decide what to do
  }
#ifdef MYLOG
  uint32_t bSize;
  const char* bl = poolToVerify.to_byte_stream(bSize);
  std::cout << "GET> PoolToVerify: " << poolToVerify.sequence() << ", tr_amount =" << poolToVerify.transactions_count() << ", " << ", " << byteStreamToHex(bl, bSize) << std::endl;
  const char* b2 = newPool.to_byte_stream(bSize);
  std::cout << "newPool: " << newPool.sequence() << ", "<< byteStreamToHex(b2, bSize) << std::endl << std::endl;
#endif
  passBlockToSolver(newPool, sender);

  getBlockChain().setGlobalSequence(newPool.sequence());
  transport_->clearTasks();

  //TODO: обновить таблицу раунда в cs::Conveyer::instance()
  // std::swap(confidants, cs::Conveyer::instance().roundTable().confidants);
  assert(false);

  onRoundStart_V3();
  // let solver to decide what to do: if (getMyLevel() == NodeLevel::Confidant) {
  solver_->gotTransactionList_V3(std::move(poolToVerify));
  //}
}
void Node::sendHash_V3() {
  /* if (myLevel_ == NodeLevel::Writer || myLevel_ == NodeLevel::Main) {
     LOG_ERROR("Writer and Main node shouldn't send hashes");
     return;
   }*/

  const auto& tmp = getBlockChain().getLastWrittenHash().to_binary();
  //cs::Hash testHash;
  //std::copy(tmp.cbegin(), tmp.cend(), testHash.begin());
 
  LOG_WARN("Sending hash of " << cs::Utils::byteStreamToHex(tmp.data(),tmp.size()) << " to ALL");
  cs::Hash tmpHash;
  std::copy(tmp.cbegin(),tmp.cend(),tmpHash.begin());

  ostream_.init(BaseFlags::Broadcast);
  ostream_ << MsgTypes::BlockHashV3
    << roundNum_
    << tmpHash;
  flushCurrentTasks();
}

void Node::getHash_V3(const uint8_t* data, const size_t size, const cs::PublicKey& sender) {
  if (myLevel_ != NodeLevel::Confidant) {
    return;
  }

  cslog() << "Get hash size: " << size;

  istream_.init(data, size);


  cs::Hash tmpHash;
  istream_ >> tmpHash;

  csdb::internal::byte_array tmp(tmpHash.cbegin(),tmpHash.cend());
 

  if (!istream_.good() || !istream_.end()) {
    cswarning() << "Bad hash packet format";
    return;
  }

  solver_->gotHash(csdb::PoolHash::from_binary(tmp), sender);
}

void Node::sendRoundInfoRequest(uint8_t respondent)
{
#ifdef MYLOG
  std::cout << "NODE> Stage THREE requesting ... ";
#endif
  if (myLevel_ != NodeLevel::Confidant) {
    LOG_WARN("Only confidant nodes can request consensus stages");
    //return;
  }

  ostream_.init(0/*need no flags!*/, cs::Conveyer::instance().roundTable().confidants.at(respondent));

  ostream_ << MsgTypes::RoundInfoRequest
    << roundNum_
    << myConfidantIndex_;
;
  flushCurrentTasks();
  LOG_DEBUG("done");
}

void Node::getRoundInfoRequest(const uint8_t* data, const size_t size, const cs::RoundNumber rNum, const cs::PublicKey& requester)
{
  LOG_DEBUG(__func__);
  //std::cout << "NODE> Getting StageThree Request" << std::endl;
  if (myLevel_ != NodeLevel::Confidant || myLevel_ != NodeLevel::Writer) {
    return;
  }
  if (myPublicKey_ == requester) {
    return;
  }
  //LOG_EVENT(FILE_NAME_ << "Getting RoundInfo Request from " << byteStreamToHex(sender.str, 32));
  istream_.init(data, size);

  uint8_t requesterNumber;
  istream_ >> requesterNumber;

  if (requester != cs::Conveyer::instance().roundTable().confidants.at(requesterNumber)) return;

  if (!istream_.good() || !istream_.end()) {
    LOG_ERROR("Bad StageThree packet format");
    return;
  }
  if(myLevel_ != NodeLevel::Confidant && rNum==roundNum_) {
    sendRoundInfoReply(requesterNumber, 0);
    return;
  }
  /*if (isConf(requester)) {
    sendRoundInfoReply(requesterNumber, 1);
    return;
  }
*/
  solver_->gotRoundInfoRequest(requesterNumber);
}

void Node::sendRoundInfoReply(uint8_t requesterNumber, uint8_t reply)
{
#ifdef MYLOG
  std::cout << "NODE> Stage THREE requesting ... ";
#endif
  if (myLevel_ != NodeLevel::Confidant) {
    LOG_WARN("Only confidant nodes can request consensus stages");
    //return;
  }

  ostream_.init(0/*need no flags!*/, cs::Conveyer::instance().roundTable().confidants.at(requesterNumber));

  ostream_ << MsgTypes::RoundInfoReply
    << roundNum_
    << myConfidantIndex_;

  flushCurrentTasks();
  LOG_DEBUG("done");
}

void Node::getRoundInfoReply(const uint8_t* data, const size_t size, const cs::RoundNumber rNum, const cs::PublicKey& respondent)
{
  LOG_DEBUG(__func__);
  //std::cout << "NODE> Getting StageThree Request" << std::endl;
  if (myLevel_ != NodeLevel::Confidant) {
    return;
  }
  if (myPublicKey_ == respondent) {
    return;
  }
  //LOG_EVENT(FILE_NAME_ << "Getting RoundInfo Request from " << byteStreamToHex(sender.str, 32));
  istream_.init(data, size);

  uint8_t requesterNumber;
  istream_ >> requesterNumber;

  if (!istream_.good() || !istream_.end()) {
    LOG_ERROR("Bad StageThree packet format");
    return;
  }
  if (respondent != cs::Conveyer::instance().roundTable().confidants.at(requesterNumber)) return;

  solver_->gotRoundInfoRequest(requesterNumber);
}

void Node::onRoundStart_V3()
{
    //if (!solver_->mPoolClosed())
    //{
    //  solver_->sendTL();
    //}
    std::cout << "======================================== ROUND " << roundNum_ << " ========================================" << std::endl;
    std::cout << "Node PK = " << cs::Utils::byteStreamToHex(myPublicKey_.data(), myPublicKey_.size()) << std::endl;
    //if(getBlockChain().getGlobalSequence()!= roundNum_-1) getBlockChain().setGlobalSequence(roundNum_-1);


    bool found = false;
    uint8_t conf_no = 0;

    const auto& conf_nodes = confidants();

    for(auto& conf : conf_nodes) {
        if(conf == myPublicKey_) {
            myLevel_ = NodeLevel::Confidant;
            myConfidantIndex_ = conf_no;
            std::cout << "===================== NODE LEVEL SET TO CONFIDANT ============================" << std::endl;
            found = true;
            //solver_->initConfRound();
            break;
        }
        conf_no++;
    }

    if(!found) {
        myLevel_ = NodeLevel::Normal;
        std::cout << "===================== NODE LEVEL SET TO NORMAL ============================" << std::endl;
    }




    // Pretty printing...
    std::cout << "Round " << roundNum_ << " started. Mynode_type:=" << myLevel_
        << std::endl << "Confidants: " << std::endl;
    int i = 0;
    for(auto& e : conf_nodes) {
        std::cout << i << ". " << cs::Utils::byteStreamToHex(e.data(), e.size()) << std::endl;
        i++;
    }
#ifdef SYNCRO
    LOG_NOTICE("Node: last written sequence = " << getBlockChain().getLastWrittenSequence());
    blockchainSync();
#endif // !1
    solver_->nextRound();
    if(!cs::Conveyer::instance().isSyncCompleted()) {
        sendHash_V3();
    }

    transport_->processPostponed(roundNum_);
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

csdb::Pool Node::getCompressedPoolFromPack()
{
    csdb::Pool pool{};

    uint32_t uncompressedSize;
    uint32_t compressedSize;
    istream_ >> uncompressedSize >> compressedSize;

    //TODO: review that condition (2) is legal
    constexpr size_t abnormal_len = 1 << 20;
    //TODO: how to get avail bytes from istream?
    if(uncompressedSize >= abnormal_len) {
        // data is corrupted
        //ptr_ = end_;
        //good_ = false;
        //return *this;
        return pool;
    }

    pool = csdb::Pool::from_lz4_byte_stream(reinterpret_cast<const char*>(istream_.getCurrPtr()),
        compressedSize, uncompressedSize);
    //ptr_ += compressedSize;
    istream_.safeSkip<uint8_t>(compressedSize);
    return pool;
}

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
            LOG_WARN("Node: prev. hash of block [" << blockSeq << "] != blockchain last hash of block [" << localSeq << "]");
            LOG_NOTICE("Blockchain last hash = " << getBlockChain().getLastHash().to_string());
            LOG_NOTICE("Block prev. hash = " << pool.previous_hash().to_string());
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