#include <algorithm>
#include <csignal>
#include <numeric>
#include <sstream>

#include <solver2/solvercore.hpp>

#include <csnode/conveyer.hpp>
#include <csnode/datastream.hpp>
#include <csnode/node.hpp>
#include <csnode/nodecore.hpp>

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

#include <poolsynchronizer.hpp>

const unsigned MIN_CONFIDANTS = 3;
const unsigned MAX_CONFIDANTS = 100;

const csdb::Address Node::genesisAddress_ =
    csdb::Address::from_string("0000000000000000000000000000000000000000000000000000000000000001");
const csdb::Address Node::startAddress_ =
    csdb::Address::from_string("0000000000000000000000000000000000000000000000000000000000000002");

Node::Node(const Config& config)
: nodeIdKey_(config.getMyPublicKey())
, bc_(config.getPathToDB().c_str(), genesisAddress_, startAddress_)
, solver_(new slv2::SolverCore(this, genesisAddress_, startAddress_))
, transport_(new Transport(config, this))
,
#ifdef MONITOR_NODE
    stats_(bc_)
,
#endif
#ifdef NODE_API
  api_(bc_, solver_)
,
#endif
  allocator_(1 << 24, 5)
, packStreamAllocator_(1 << 26, 5)
, ostream_(&packStreamAllocator_, nodeIdKey_)
, poolSynchronizer_(new cs::PoolSynchronizer(config.getPoolSyncSettings(), transport_, &bc_)) {
  good_ = init();
}

Node::~Node() {
  sendingTimer_.stop();

  delete solver_;
  delete transport_;
  delete poolSynchronizer_;
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
  poolSynchronizer_->processingSync(roundNum_);
}

void Node::run() {
  transport_->run();
}

void Node::stop() {
  solver_->finish();
  cswarning() << "[SOLVER STOPPED]";

  auto bcStorage = bc_.getStorage();
  bcStorage.close();

  cswarning() << "[BLOCKCHAIN STORAGE CLOSED]";
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
    cswarning() << "NODE> Last written sequence lower than current round - 1";
    return;
  }

  // start round on node

  // TODO: think how to improve this code
  cslog() << "NODE> Get Round table SS -> got Round = " << rNum;

  cs::Timer::singleShot(TIME_TO_AWAIT_SS_ROUND, [this, roundTable]() mutable {
    onRoundStart_V3(roundTable);
    onRoundStartConveyer(std::move(roundTable));
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
  cslog() << "Round " << roundNum_
          << ", General: " << cs::Utils::byteStreamToHex(roundTable.general.data(), roundTable.general.size())
          << "Confidants: ";

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

template <typename... Args>

bool Node::sendNeighbours(const cs::PublicKey& target, const MsgTypes& msgType, const cs::RoundNumber round, Args&&... args) {
  ConnectionPtr connection = transport_->getConnectionByKey(target);

  if (connection) {
    sendNeighbours(connection, msgType, round, std::forward<Args>(args)...);
  }

  return static_cast<bool>(connection);
}

template <typename... Args>
void Node::sendNeighbours(const ConnectionPtr& target, const MsgTypes& msgType, const cs::RoundNumber round, Args&&... args) {
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
void Node::sendBroadcast(const MsgTypes& msgType, const cs::RoundNumber round, Args&&... args) {
  ostream_.init(BaseFlags::Broadcast | BaseFlags::Fragmented | BaseFlags::Compressed);
  csdebug() << "NODE> Sending broadcast";

  sendBroadcastImpl(msgType, round, std::forward<Args>(args)...);
}

template <class... Args>
void Node::tryToSendDirect(const cs::PublicKey& target, const MsgTypes& msgType, const cs::RoundNumber round, Args&&... args) {
  const bool success = sendNeighbours(target, msgType, round, std::forward<Args>(args)...);
  if (!success) {
    sendBroadcast(target, msgType, round, std::forward<Args>(args)...);
  }
}

template <class... Args>
bool Node::sendToRandomNeighbour(const MsgTypes& msgType, const cs::RoundNumber round, Args&&... args) {
  ConnectionPtr target = transport_->getRandomNeighbour();

  if (target) {
    sendNeighbours(target, msgType, round, std::forward<Args>(args)...);
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

void Node::getPacketHashesRequest(const uint8_t* data, const std::size_t size, const cs::RoundNumber round,
                                  const cs::PublicKey& sender) {
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

  cslog() << "NODE> Requested packet hashes: " << hashesCount;

  if (hashesCount != hashes.size()) {
    cserror() << "NODE> wrong hashes list requested";
    return;
  }

  processPacketsRequest(std::move(hashes), round, sender);
}

void Node::getPacketHashesReply(const uint8_t* data, const std::size_t size, const cs::RoundNumber round,
                                const cs::PublicKey& sender) {
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

void Node::getCharacteristic(const uint8_t* data, const size_t size, const cs::RoundNumber round,
                             const cs::PublicKey& sender) {
  cslog() << "NODE> " << __func__ << "():";
  cs::Conveyer& conveyer = cs::Conveyer::instance();

  if (!conveyer.isSyncCompleted(round)) {
    cslog() << "\tPacket sync not finished, saving characteristic meta to call after sync";

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

  cslog() << "\tCharacteristic data size: " << size;

  istream_ >> time;
  istream_ >> characteristicMask >> sequence;

  cs::PoolMetaInfo poolMetaInfo;
  poolMetaInfo.sequenceNumber = sequence;
  poolMetaInfo.timestamp = std::move(time);

  cs::Signature signature;
  istream_ >> signature;

  cslog() << "\tsequence " << poolMetaInfo.sequenceNumber << ", mask size " << characteristicMask.size();
  cslog() << "\tTime " << poolMetaInfo.timestamp;

  cs::Characteristic characteristic;
  characteristic.mask = std::move(characteristicMask);
  conveyer.setCharacteristic(std::move(characteristic), round);

  assert(sequence <= this->getRoundNumber());

  cs::PublicKey writerPublicKey;
  istream_ >> writerPublicKey;

  if (!istream_.good()) {
    cserror() << "NODE> getCharacteristic(): packet parsing failed";
    return;
  }

  std::optional<csdb::Pool> pool = conveyer.applyCharacteristic(poolMetaInfo, writerPublicKey);

  if (!pool.has_value()) {
    cserror() << "NODE> getCharacteristic(): created pool is not valid";
    return;
  }

  if(!getBlockChain().storeBlock(pool.value(), signature)) {
    cserror() << "NODE> failed to store block in BlockChain";
  }

  csdebug() << "NODE> " << __func__ << "(): done";
}

const cs::ConfidantsKeys& Node::confidants() const {
  return cs::Conveyer::instance().currentRoundTable().confidants;
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

void Node::applyNotifications()
{
  cserror() << "NODE> Apply notifications is obsolete in current version";
}

bool Node::isCorrectNotification(const uint8_t* data, const std::size_t size) {
  cs::DataStream stream(data, size);

  cs::Hash characteristicHash;
  stream >> characteristicHash;

  cs::Conveyer& conveyer = cs::Conveyer::instance();
  cs::Hash currentCharacteristicHash = conveyer.characteristicHash(conveyer.currentRoundNumber());

  if (characteristicHash != currentCharacteristicHash) {
    csdebug() << "NODE> Characteristic equals failed";
    csdebug() << "NODE> Received characteristic - "
              << cs::Utils::byteStreamToHex(characteristicHash.data(), characteristicHash.size());
    csdebug() << "NODE> Writer solver chracteristic - "
              << cs::Utils::byteStreamToHex(currentCharacteristicHash.data(), currentCharacteristicHash.size());
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

void Node::createBlockValidatingPacket(const cs::PoolMetaInfo& poolMetaInfo, const cs::Characteristic& characteristic,
                                       const cs::Signature& signature, const cs::Notifications& notifications) {
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
  const cs::Signature& signature/*,
  const cs::Notifications& notifications*/) {
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
  cs::Hash characteristicHash = cs::Conveyer::instance().characteristicHash(roundNum_);

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

  cslog() << "NODE> Sending hash " << roundNum_ << " to " << cs::Utils::byteStreamToHex(target.data(), target.size());
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

void Node::sendPacketHashesRequest(const cs::Hashes& hashes, const cs::RoundNumber round, uint32_t requestStep) {
  if (cs::Conveyer::instance().isSyncCompleted(round)) {
    return;
  }

  csdebug() << "NODE> Sending packet hashes request: " << hashes.size();

  cs::PublicKey main;
  const auto msgType = MsgTypes::TransactionsPacketRequest;
  const auto roundTable = cs::Conveyer::instance().roundTable(round);

  // look at main node
  main = (roundTable != nullptr) ? roundTable->general : cs::Conveyer::instance().currentRoundTable().general;

  const bool sendToGeneral = sendNeighbours(main, msgType, round, hashes);

  if (!sendToGeneral) {
    csdebug() << "NODE> Sending packet to main node " << cs::Utils::byteStreamToHex(main.data(), main.size());
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
  cs::Timer::singleShot(cs::NeighboursRequestDelay + requestStep, requestClosure);
}

void Node::sendPacketHashesRequestToRandomNeighbour(const cs::Hashes& hashes, const cs::RoundNumber round) {
  const auto msgType = MsgTypes::TransactionsPacketRequest;
  const auto neighboursCount = transport_->getNeighboursCount();

  bool successRequest = false;

  for (std::size_t i = 0; i < neighboursCount; ++i) {
    ConnectionPtr connection = transport_->getNeighbourByNumber(i);

    if (connection) {
      successRequest = true;
      sendNeighbours(connection, msgType, round, hashes);
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
  const bool success = sendNeighbours(target, msgType, round, packets);

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

  cslog() << "NODE> Get block request> <<< Getting the request for block: from: " << sequences.front() << ", to: " << sequences.back() << ",  packet: " << packetNum;

  if (sequencesCount != sequences.size()) {
    cserror() << "Bad sequences created";
    return;
  }

  if (sequences.front() > bc_.getLastWrittenSequence()) {
    cslog() << "NODE> Get block request> The requested block: " << sequences.front() << " is BEYOND my CHAIN";
    return;
  }

  cs::PoolsBlock poolsBlock;
  poolsBlock.reserve(sequencesCount);

  for (auto& sequence : sequences) {
    csdb::Pool pool = bc_.loadBlock(bc_.getHashBySequence(sequence));

    if (pool.is_valid()) {
      auto prev_hash = csdb::PoolHash::from_string("");
      pool.set_previous_hash(prev_hash);

      poolsBlock.push_back(std::move(pool));
    }
  }

  sendBlockReply(poolsBlock, sender, packetNum);
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
    const auto round = cs::Conveyer::instance().currentRoundNumber();
    csdebug() << "NODE> " << __func__
        << "() Target out(): " << cs::Utils::byteStreamToHex(target.data(), target.size())
        << ", packet: " << packetNum
        << ", round: " << round;
  
  for (const auto& pool : poolsBlock) {
    csdebug() << "NODE> Send block reply. Sequence: " << pool.sequence();
  }

  tryToSendDirect(target, MsgTypes::RequestedBlock, round, poolsBlock, packetNum);
}

void Node::becomeWriter() {
  myLevel_ = NodeLevel::Writer;
  cslog() << "NODE> Became writer";

  // if (cs::Conveyer::instance().isEnoughNotifications(cs::Conveyer::NotificationState::GreaterEqual)) {
  //  applyNotifications();
  //}
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

void Node::onRoundStartConveyer(cs::RoundTable&& roundTable) {
  cs::Conveyer& conveyer = cs::Conveyer::instance();
  conveyer.setRound(std::move(roundTable));
  const auto& rt = conveyer.currentRoundTable();

  if (rt.hashes.empty() || conveyer.isSyncCompleted()) {
    if (rt.hashes.empty()) {
      cslog() << "NODE> No hashes in round table - > start consensus now";
    }
    else {
      cslog() << "NODE> All hashes in conveyer -> start consensus now";
    }

    startConsensus();
  }
  else {
    sendPacketHashesRequest(conveyer.currentNeededHashes(), roundNum_, startPacketRequestPoint_);
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

void Node::sendBlockRequest(const ConnectionPtr& target, const cs::PoolsRequestedSequences sequences, uint32_t packetNum) {
    const auto round = cs::Conveyer::instance().currentRoundNumber();
    csdebug() << "NODE> " << __func__
        << "() Target out(): " << target->getOut()
        << ", sequence from: " << sequences.front() << ", to: " << sequences.back()
        << ", packet: " << packetNum
        << ", round: " << round;

  ostream_.init(BaseFlags::Neighbours | BaseFlags::Signed | BaseFlags::Compressed);
  ostream_ << MsgTypes::BlockRequest << round << sequences << packetNum;

  transport_->deliverDirect(ostream_.getPackets(), ostream_.getPacketsCount(), target);

  ostream_.clear();
}

void Node::initNextRound(std::vector<cs::PublicKey>&& confidantNodes) {
  cslog() << "Node: init next round1";
  // copied from Solver::gotHash():
  cs::Hashes hashes;
  cs::Conveyer& conveyer = cs::Conveyer::instance();
  cs::RoundNumber round = conveyer.currentRoundNumber();

  {
    cs::SharedLock lock(conveyer.sharedMutex());
    for (const auto& element : conveyer.transactionsPacketTable()) {
      hashes.push_back(element.first);
    }
  }

  cs::RoundTable table;
  table.round = ++round;
  table.confidants = std::move(confidantNodes);
  // table.general = mainNode;

  table.hashes = std::move(hashes);
  conveyer.setRound(std::move(table));

  initNextRound(conveyer.currentRoundTable());
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

  if (type == MsgTypes::RoundTableRequest) {
    return (rNum < roundNum_ ? MessageActions::Process : MessageActions::Drop);
  }

  if (type == MsgTypes::RoundInfoRequest) {
    return (rNum <= roundNum_ ? MessageActions::Process : MessageActions::Drop);
  }

  if (type == MsgTypes::RoundInfoReply) {
    return (rNum >= roundNum_ ? MessageActions::Process : MessageActions::Drop);
  }

  if (type == MsgTypes::BlockRequest || type == MsgTypes::RequestedBlock) {
    // which round would not be on the remote we may require the requested block
    return MessageActions::Process;
    // return (rNum <= roundNum_ ? MessageActions::Process : MessageActions::Drop);
  }

  if (type == MsgTypes::BlockHashV3) {
    if (rNum < roundNum_) {
      return MessageActions::Drop;
    }
    if (rNum == roundNum_ && cs::Conveyer::instance().isSyncCompleted()) {
      return MessageActions::Process;
    }
    if (rNum != roundNum_) {
      cslog() << "NODE> outrunning block hash (#" << rNum << ") is postponed until get round info";
    }
    else {
      cslog() << "NODE> block hash is postponed until conveyer sync is completed";
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

void Node::showSyncronizationProgress(csdb::Pool::sequence_t lastWrittenSequence,
                                      csdb::Pool::sequence_t globalSequence) {
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

std::ostream& operator<<(std::ostream& os, NodeLevel nodeLevel) {
  os << nodeLevelToString(nodeLevel);
  return os;
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

template <typename... Args>
void Node::sendBroadcast(const cs::PublicKey& target, const MsgTypes& msgType, const cs::RoundNumber round,
                         Args&&... args) {
  ostream_.init(BaseFlags::Fragmented | BaseFlags::Compressed, target);
  csdebug() << "NODE> Sending broadcast to: " << cs::Utils::byteStreamToHex(target.data(), target.size());

  sendBroadcastImpl(msgType, round, std::forward<Args>(args)...);
}

template <typename... Args>
void Node::sendBroadcastImpl(const MsgTypes& msgType, const cs::RoundNumber round, Args&&... args) {
  ostream_ << msgType << round;

  writeDefaultStream(std::forward<Args>(args)...);

  csdebug() << "NODE> Sending Direct data: size = " << ostream_.getCurrentSize()
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
    cswarning() << "Only confidant nodes can send consensus stages";
    return;
  }
  csdebug() << __func__ << ": round = " << roundNum_ << " sender: " << (int)stageOneInfo.sender
            << " , hash: " << cs::Utils::byteStreamToHex(stageOneInfo.hash.data(), stageOneInfo.hash.size())
            << " , cand. amount: " << (int)stageOneInfo.candidatesAmount;

  size_t msgSize = stageOneInfo.hash.size() + sizeof(stageOneInfo.sender) + sizeof(stageOneInfo.candidatesAmount) +
                   stageOneInfo.candiates[0].size() *
                       (size_t)stageOneInfo.candidatesAmount;  // hash size + 2*sizeof(uint8_t) + sizeof(PublicKey) * n
  auto memPtr = allocator_.allocateNext(msgSize);
  uint8_t* rawData = (uint8_t*)memPtr.get();

  uint8_t* ptr = rawData;
  *ptr = stageOneInfo.sender;
  ptr += sizeof(stageOneInfo.sender);
  memcpy(ptr, stageOneInfo.hash.data(), stageOneInfo.hash.size());
  ptr += stageOneInfo.hash.size();
  *ptr = stageOneInfo.candidatesAmount;
  ptr += sizeof(stageOneInfo.candidatesAmount);
  for (size_t i = 0; i < (size_t)stageOneInfo.candidatesAmount; ++i) {
    memcpy(ptr, stageOneInfo.candiates[i].data(), stageOneInfo.candiates[i].size());
    ptr += stageOneInfo.candiates[i].size();
  }
  assert(ptr >= rawData);
  assert(static_cast<size_t>(ptr - rawData) == msgSize);

  // cslog() << "Sent message: (" << msgSize << ") : " << byteStreamToHex((const char*)rawData, msgSize);

  unsigned long long sig_size;
  crypto_sign_ed25519_detached(stageOneInfo.sig.data(), &sig_size, rawData, msgSize, solver_->getPrivateKey().data());
  // cslog() << " Sig: " << byteStreamToHex((const char*)stageOneInfo.sig.val, 64);
  ostream_.init(BaseFlags::Broadcast);
  ostream_ << MsgTypes::FirstStage << roundNum_ << msgSize << stageOneInfo.sig;
  ostream_ << std::string(cs::numeric_cast<const char*>((void*)rawData), msgSize);
  allocator_.shrinkLast(msgSize);
  flushCurrentTasks();
}

// sends StageOne request to respondent about required
void Node::requestStageOne(uint8_t respondent, uint8_t required) {
#ifdef MYLOG
  cslog() << "NODE> Stage ONE requesting ... ";
#endif
  if (myLevel_ != NodeLevel::Confidant) {
    cswarning() << "Only confidant nodes can request consensus stages";
    // return;
  }

  ostream_.init(0 /*need no flags!*/, cs::Conveyer::instance().currentRoundTable().confidants.at(respondent));
  ostream_ << MsgTypes::FirstStageRequest << roundNum_ << myConfidantIndex_ << required;
  flushCurrentTasks();
  LOG_DEBUG("done");
}

void Node::getStageOneRequest(const uint8_t* data, const size_t size, const cs::PublicKey& requester) {
  LOG_DEBUG(__func__);
  // cslog() << "NODE> Getting StageOne Request";
  if (myLevel_ != NodeLevel::Confidant) {
    return;
  }
  // cslog() << "NODE> Getting StageOne 0";
  if (nodeIdKey_ == requester) {
    return;
  }
  // cslog() << "NODE> Getting StageOne 1";
  // cslog() << "Getting Stage One Request from " << byteStreamToHex(sender.str, 32));
  istream_.init(data, size);
  uint8_t requesterNumber = 0;
  uint8_t requiredNumber = 0;
  istream_ >> requesterNumber >> requiredNumber;

  if (requester != cs::Conveyer::instance().currentRoundTable().confidants.at(requesterNumber)) {
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
    // return;
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
    // cslog() << i << ". " << byteStreamToHex(stageOneInfo.candiates[i].str, 32);
  }

  ostream_.init(0 /*need no flags!*/, cs::Conveyer::instance().currentRoundTable().confidants.at(requester));

  ostream_ << MsgTypes::FirstStage << roundNum_ << msgSize << stageOneInfo.sig;
  ostream_ << std::string(cs::numeric_cast<const char*>((void*)rawData), msgSize);
  csdebug() << "Round = " << roundNum_ << " Sender: " << (int)stageOneInfo.sender << std::endl
            << " Hash: " << cs::Utils::byteStreamToHex(stageOneInfo.hash.data(), stageOneInfo.hash.size())
            << " Cand Amount: " << (int)(stageOneInfo.candidatesAmount)
            << " Sig: " << cs::Utils::byteStreamToHex(stageOneInfo.sig.data(), stageOneInfo.sig.size());

  for (int i = 0; i < stageOneInfo.candidatesAmount; i++) {
    csdebug() << i << ". "
              << cs::Utils::byteStreamToHex(stageOneInfo.candiates[i].data(), stageOneInfo.candiates[i].size());
  }
  flushCurrentTasks();
  allocator_.shrinkLast(msgSize);
  csdebug() << "NODE> " << __func__ << "(): done";
}

void Node::getStageOne(const uint8_t* data, const size_t size, const cs::PublicKey& sender) {
  csdebug() << __func__;
  // cslog() << "NODE> Getting StageOne";
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
  istream_ >> msgSize >> stage.sig;

  std::string raw_bytes;
  istream_ >> raw_bytes;
  if (!istream_.good() || !istream_.end()) {
    cserror() << "Bad StageOne packet format";
    return;
  }

  const uint8_t* stagePtr = (const uint8_t*)raw_bytes.data();

  auto memPtr = allocator_.allocateNext(msgSize);
  uint8_t* rawData = (uint8_t*)memPtr.get();

  memcpy(rawData, stagePtr, msgSize);

  // cslog() << "Received message: "<< byteStreamToHex((const char*)stagePtr , msgSize);

  stage.sender = *rawData;
  if (crypto_sign_ed25519_verify_detached(
          stage.sig.data(), rawData, msgSize,
          (const unsigned char*)cs::Conveyer::instance().currentRoundTable().confidants.at(stage.sender).data())) {
    cswarning() << "NODE> Stage One from [" << (int)stage.sender << "] -  WRONG SIGNATURE!!!";
    return;
  }
  // cslog() << "NODE> Signature is OK!" ;
  memcpy(stage.hash.data(), rawData + 1, 32);
  stage.candidatesAmount = *(rawData + 33);
  for (int i = 0; i < stage.candidatesAmount; i++) {
    memcpy(stage.candiates[i].data(), rawData + 34 + 32 * i, 32);
    //  csdebug() << i << ". " << cs::Utils::byteStreamToHex(stage.candiates[i].data(), stage.candiates[i].size());
  }
  allocator_.shrinkLast(msgSize);
  // csdebug() << "Size: " << msgSize << "  Sender: " << (int)stage.sender << std::endl
  //  << " Hash: " << cs::Utils::byteStreamToHex(stage.hash.data(), stage.hash.size())
  //  << " Cand Amount: " << (int)stage.candidatesAmount << std::endl
  //  << " Sig: " << cs::Utils::byteStreamToHex(stage.sig.data(), stage.sig.size());

  solver_->gotStageOne(std::move(stage));
}

void Node::sendStageTwo(const cs::StageTwo& stageTwoInfo) {
#ifdef MYLOG
  cslog() << "NODE> +++++++++++++++++++++++ Stage TWO sending +++++++++++++++++++++++++++";
#endif
  if ((myLevel_ != NodeLevel::Confidant) && (myLevel_ != NodeLevel::Writer)) {
    cswarning() << "Only confidant nodes can send consensus stages";
    return;
  }

  size_t msgSize = 2 + 64 * stageTwoInfo.trustedAmount;
  auto memPtr = allocator_.allocateNext(msgSize);
  uint8_t* rawData = (uint8_t*)memPtr.get();

  *rawData = stageTwoInfo.sender;
  *(rawData + 1) = stageTwoInfo.trustedAmount;
  for (int i = 0; i < stageTwoInfo.trustedAmount; i++) {
    memcpy(rawData + 2 * sizeof(uint8_t) + i * sizeof(cs::Signature), stageTwoInfo.signatures[i].data(),
           stageTwoInfo.signatures[i].size());
  }
  // cslog() << "Sent message: (" << msgSize << ") : " << byteStreamToHex((const char*)rawData, msgSize;

  unsigned long long sig_size;
  crypto_sign_ed25519_detached((unsigned char*)stageTwoInfo.sig.data(), &sig_size, rawData, msgSize,
                               solver_->getPrivateKey().data());

  ostream_.init(BaseFlags::Broadcast);
  ostream_ << MsgTypes::SecondStage << roundNum_ << msgSize << stageTwoInfo.sig;
  ostream_ << std::string(cs::numeric_cast<const char*>((void*)rawData), msgSize);

  // cslog()<< "NODE> Sending StageTwo:";
  // for (int i = 0; i < stageTwoInfo.trustedAmount; i++) {
  //  cslog() << " Sig[" << i << "]: " << byteStreamToHex((const char*)stageTwoInfo.signatures[i].val, 64);
  //}
  allocator_.shrinkLast(msgSize);
  LOG_DEBUG("done");
  flushCurrentTasks();
}

void Node::requestStageTwo(uint8_t respondent, uint8_t required) {
  if ((myLevel_ != NodeLevel::Confidant) && (myLevel_ != NodeLevel::Writer)) {
    cswarning() << "Only confidant nodes can request consensus stages";
    return;
  }

  cslog() << "==============================";
  cslog() << "NODE> Stage TWO requesting ... ";
  cslog() << "==============================";

  ostream_.init(0 /*need no flags!*/, cs::Conveyer::instance().currentRoundTable().confidants.at(respondent));

  ostream_ << MsgTypes::SecondStageRequest << roundNum_ << myConfidantIndex_ << required;
  flushCurrentTasks();
  LOG_DEBUG("done");
}

void Node::getStageTwoRequest(const uint8_t* data, const size_t size, const cs::PublicKey& requester) {
  LOG_DEBUG(__func__);

  if ((myLevel_ != NodeLevel::Confidant) && (myLevel_ != NodeLevel::Writer)) {
    return;
  }
  if (nodeIdKey_ == requester) {
    return;
  }

  istream_.init(data, size);
  uint8_t requesterNumber = 0u;
  uint8_t requiredNumber =0u;
  istream_ >> requesterNumber >> requiredNumber;

  cslog() << "NODE> Getting StageTwo Request from [" << (int)requesterNumber << "] ";
  if (requester != cs::Conveyer::instance().currentRoundTable().confidants.at(requesterNumber))
    return;

  if (!istream_.good() || !istream_.end()) {
    cserror() << "Bad StageTwo packet format";
    return;
  }
  solver_->gotStageTwoRequest(requesterNumber, requiredNumber);
}

void Node::sendStageTwoReply(const cs::StageTwo& stageTwoInfo, const uint8_t requester) {
  cslog() << "NODE> Stage Two REPLY sendiing";
  if ((myLevel_ != NodeLevel::Confidant) && (myLevel_ != NodeLevel::Writer)) {
    cswarning() << "Only confidant nodes can send consensus stages";
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
  // cslog() << "Sent message: (" << msgSize << ") : " << byteStreamToHex((const char*)rawData, msgSize);

  ostream_.init(0 /*need no flags!*/, cs::Conveyer::instance().currentRoundTable().confidants.at(requester));

  ostream_ << MsgTypes::SecondStage << roundNum_ << msgSize << stageTwoInfo.sig;
  ostream_ << std::string(cs::numeric_cast<const char*>((void*)rawData), msgSize);
  // cslog() << "NODE> Sending StageTwo:";
  // for (int i = 0; i < stageTwoInfo.trustedAmount; i++) {
  // cslog() << " Sig[" << i << "]: " << byteStreamToHex((const char*)stageTwoInfo.signatures[i].val, 64);
  //}
  allocator_.shrinkLast(msgSize);
  LOG_DEBUG("done");
  flushCurrentTasks();
}

void Node::getStageTwo(const uint8_t* data, const size_t size, const cs::PublicKey& sender) {
  LOG_DEBUG(__func__);
  // cslog() << "NODE> Getting StageTwo";
  if ((myLevel_ != NodeLevel::Confidant) && (myLevel_ != NodeLevel::Writer)) {
    return;
  }
  if (nodeIdKey_ == sender) {
    return;
  }
  // LOG_EVENT(FILE_NAME_ << "Getting Stage Two from " << byteStreamToHex(sender.str, 32));

  istream_.init(data, size);
  size_t msgSize;
  cs::StageTwo stage;
  istream_ >> msgSize >> stage.sig;

  std::string raw_bytes;
  istream_ >> raw_bytes;
  if (!istream_.good() || !istream_.end()) {
    cserror() << "Bad StageTwo packet format";
    return;
  }

  const uint8_t* stagePtr = (uint8_t*)raw_bytes.data();
  auto memPtr = allocator_.allocateNext(msgSize);
  uint8_t* rawData = (uint8_t*)memPtr.get();
  memcpy(rawData, stagePtr, msgSize);

  stage.sender = *rawData;
  // cslog() << "Received message: "<< byteStreamToHex((const char*)stagePtr, msgSize);
  if (crypto_sign_ed25519_verify_detached(
          stage.sig.data(), rawData, msgSize,
          (const unsigned char*)cs::Conveyer::instance().currentRoundTable().confidants.at(stage.sender).data())) {
    cslog() << "NODE> Stage Two from [" << (int)stage.sender << "] -  WRONG SIGNATURE!!!";
    return;
  }

  // cslog() << "NODE> Stage Two [" << (int)stage.sender << "] signature is OK!";
  // cslog() << "Package Signature: " << byteStreamToHex((const char*)stage.sig.val, 64);
  stage.trustedAmount = *(rawData + 1);
  for (int i = 0; i < stage.trustedAmount; i++) {
    memcpy(stage.signatures[i].data(), rawData + 2 + 64 * i, 64);
    // cslog() << " Sig[" << i << "]: " << byteStreamToHex((const char*)stage.signatures[i].val, 64);
  }

  allocator_.shrinkLast(msgSize);
  solver_->gotStageTwo(std::move(stage));
}

void Node::sendStageThree(const cs::StageThree& stageThreeInfo) {
  LOG_DEBUG(__func__);
#ifdef MYLOG
  cslog() << "NODE> Stage THREE sending";
#endif
  if (myLevel_ != NodeLevel::Confidant) {
    cswarning() << "Only confidant nodes can send consensus stages";
    return;
  }

  size_t msgSize = 66;  // = 2*sizeof(uint8_t) + 2*sizeof(Hash);
  auto memPtr = allocator_.allocateNext(msgSize);
  uint8_t* rawData = (uint8_t*)memPtr.get();

  *rawData = stageThreeInfo.sender;
  *(rawData + 1) = stageThreeInfo.writer;
  memcpy(rawData + 2, stageThreeInfo.hashBlock.data(), 32);
  memcpy(rawData + 34, stageThreeInfo.hashCandidatesList.data(), 32);

  unsigned long long sig_size;
  crypto_sign_ed25519_detached((unsigned char*)stageThreeInfo.sig.data(), &sig_size, rawData, msgSize,
                               solver_->getPrivateKey().data());

  ostream_.init(BaseFlags::Broadcast);
  ostream_ << MsgTypes::ThirdStage << roundNum_ << msgSize << stageThreeInfo.sig;
  ostream_ << std::string(cs::numeric_cast<const char*>((void*)rawData), msgSize);
  allocator_.shrinkLast(msgSize);
  LOG_DEBUG("done");
  flushCurrentTasks();
}

void Node::requestStageThree(uint8_t respondent, uint8_t required) {
#ifdef MYLOG
  cslog() << "NODE> Stage THREE requesting ... ";
#endif
  if (myLevel_ != NodeLevel::Confidant) {
    cswarning() << "Only confidant nodes can request consensus stages";
    // return;
  }

  ostream_.init(0 /*need no flags!*/, cs::Conveyer::instance().currentRoundTable().confidants.at(respondent));

  ostream_ << MsgTypes::ThirdStageRequest << roundNum_ << myConfidantIndex_ << required;
  flushCurrentTasks();
  LOG_DEBUG("done");
}

void Node::getStageThreeRequest(const uint8_t* data, const size_t size, const cs::PublicKey& requester) {
  LOG_DEBUG(__func__);
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

  if (requester != cs::Conveyer::instance().currentRoundTable().confidants.at(requesterNumber)) {
    return;
  }

  if (!istream_.good() || !istream_.end()) {
    cserror() << "Bad StageThree packet format";
    return;
  }
  solver_->gotStageThreeRequest(requesterNumber, requiredNumber);
}

void Node::sendStageThreeReply(const cs::StageThree& stageThreeInfo, const uint8_t requester) {
  LOG_DEBUG(__func__);
#ifdef MYLOG
  cslog() << "NODE> Stage THREE Reply sending";
#endif
  if (myLevel_ != NodeLevel::Confidant) {
    cswarning() << "Only confidant nodes can send consensus stages";
    return;
  }

  size_t msgSize = 2 * sizeof(uint8_t) + 2 * sizeof(cs::Hash);
  auto memPtr = allocator_.allocateNext(msgSize);
  uint8_t* rawData = (uint8_t*)memPtr.get();

  *rawData = stageThreeInfo.sender;
  *(rawData + 1) = stageThreeInfo.writer;
  memcpy(rawData + 2, stageThreeInfo.hashBlock.data(), 32);
  memcpy(rawData + 34, stageThreeInfo.hashCandidatesList.data(), 32);

  ostream_.init(0 /*need no flags!*/, cs::Conveyer::instance().currentRoundTable().confidants.at(requester));

  ostream_ << MsgTypes::ThirdStage << roundNum_ << msgSize << stageThreeInfo.sig;
  ostream_ << std::string(cs::numeric_cast<const char*>((void*)rawData), msgSize);
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
  // cslog()<< "NODE> Getting Stage Three  ";
  // LOG_EVENT(FILE_NAME_ << "Getting Stage Three from " << byteStreamToHex(sender.str, 32));
  size_t msgSize;
  istream_.init(data, size);
  cs::StageThree stage;
  istream_ >> msgSize >> stage.sig;
  std::string raw_bytes;
  istream_ >> raw_bytes;
  const uint8_t* stagePtr = (uint8_t*)raw_bytes.data();
  auto memPtr = allocator_.allocateNext(msgSize);
  uint8_t* rawData = (uint8_t*)memPtr.get();
  memcpy(rawData, stagePtr, msgSize);

  if (!istream_.good() || !istream_.end()) {
    cserror() << "Bad StageTwo packet format";
    return;
  }
  stage.sender = *rawData;

  // cslog() << "Received message: "<< byteStreamToHex((const char*)rawData, msgSize);
  if (crypto_sign_ed25519_verify_detached(
          stage.sig.data(), rawData, msgSize,
          (const unsigned char*)cs::Conveyer::instance().currentRoundTable().confidants.at(stage.sender).data())) {
    cslog() << "NODE> Stage Three from [" << (int)stage.sender << "] -  WRONG SIGNATURE!!!";
    return;
  }
  // cslog() << "NODE> Signature is OK!";
  stage.writer = *(rawData + 1);
  memcpy(stage.hashBlock.data(), rawData + 2, 32);
  memcpy(stage.hashCandidatesList.data(), rawData + 34, 32);
  allocator_.shrinkLast(msgSize);
  solver_->gotStageThree(std::move(stage));
}

void Node::prepareMetaForSending(cs::RoundTable& roundTable) {
  csdebug() << "NODE> " << __func__ << "():";
  // only for new consensus
  cs::PoolMetaInfo poolMetaInfo;
  poolMetaInfo.sequenceNumber = bc_.getLastWrittenSequence() + 1;  // change for roundNumber
  poolMetaInfo.timestamp = cs::Utils::currentTimestamp();

  /////////////////////////////////////////////////////////////////////////// preparing block meta info
  cs::Conveyer& conveyer = cs::Conveyer::instance();
  std::optional<csdb::Pool> pool = conveyer.applyCharacteristic(poolMetaInfo, solver_->getPublicKey());
  if (!pool.has_value()) {
    cserror() << "NODE> APPLY CHARACTERISTIC ERROR!";
    return;
  }

  pool = getBlockChain().createBlock(pool.value(), solver_->getPrivateKey());
  if(!pool.has_value()) {
    cserror() << "NODE> CREATE BLOCK ERROR!";
    return;
  }

  // array
  cs::Signature poolSignature;
  const auto& signature = pool.value().signature();
  std::copy(signature.begin(), signature.end(), poolSignature.begin());

  csdebug() << "\tsignature: " << cs::Utils::byteStreamToHex(poolSignature.data(), poolSignature.size());
  csdebug() << "\tverify signature: " << pool.value().verify_signature();

  logPool(pool.value());
  sendRoundInfo(roundTable, poolMetaInfo, poolSignature);
}

void Node::sendRoundInfo(cs::RoundTable& roundTable, cs::PoolMetaInfo poolMetaInfo, cs::Signature poolSignature) {
  cs::Conveyer& conveyer = cs::Conveyer::instance();
  roundNum_ = roundTable.round;
  // update hashes in round table here, they are free of stored packets' hashes
  if (!roundTable.hashes.empty()) {
    roundTable.hashes.clear();
  }
  {
    cs::SharedLock lock(conveyer.sharedMutex());
    for (const auto& element : conveyer.transactionsPacketTable()) {
      roundTable.hashes.push_back(element.first);
    }
  }
  const cs::Characteristic* block_characteristic = conveyer.characteristic(conveyer.currentRoundNumber());

  if (!block_characteristic) {
    cserror() << "Send round info characteristic not found, logic error";
    return;
  }

  conveyer.setRound(std::move(roundTable));
  /////////////////////////////////////////////////////////////////////////// sending round info and block
  createRoundPackage(conveyer.currentRoundTable(), poolMetaInfo, *block_characteristic, poolSignature);
  storeRoundPackageData(conveyer.currentRoundTable(), poolMetaInfo, *block_characteristic, poolSignature);

  flushCurrentTasks();

  /////////////////////////////////////////////////////////////////////////// screen output
  cslog() << "------------------------------------------  SendRoundTable  ---------------------------------------";
  const cs::RoundTable& table = conveyer.currentRoundTable();
  const cs::ConfidantsKeys confidants = table.confidants;
  cslog() << "Round " << roundNum_ << ", Confidants count " << confidants.size();

  // for (std::size_t i = 0; i < confidants.size(); ++i) {
  //  const cs::PublicKey& confidant = confidants[i];
  //  if (confidant != table.general) {
  //    cslog() << i << ". " << cs::Utils::byteStreamToHex(confidant.data(), confidant.size());
  //  }
  //}

  const cs::Hashes& hashes = table.hashes;
  cslog() << "Hashes count: " << hashes.size();

  // for (std::size_t i = 0; i < hashes.size(); ++i) {
  //  csdebug() << i << ". " << hashes[i].toString();
  //}

  transport_->clearTasks();

  onRoundStart_V3(table);
  startConsensus();
}

void Node::getRoundInfo(const uint8_t* data, const size_t size, const cs::RoundNumber rNum,
                        const cs::PublicKey& sender) {
  cslog() << "\nNODE> " << __func__ << "():";

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

  cs::Hashes hashes;
  hashes.reserve(hashesCount);

  for (std::size_t i = 0; i < hashesCount; ++i) {
    cs::TransactionsPacketHash hash;
    istream_ >> hash;

    hashes.push_back(hash);
  }

  roundTable.confidants = std::move(confidants);
  roundTable.hashes = std::move(hashes);
  roundTable.general = sender;

  const cs::ConfidantsKeys confidants_ = roundTable.confidants;
  cslog() << "\tconfidants: " << confidants_.size();
  ///////////////////////////////////// Round table received

  ///////////////////////////////////// Parcing char func

  cs::Conveyer& conveyer = cs::Conveyer::instance();

  // rNum has been incremented just before:
  if (!conveyer.isSyncCompleted(conveyer.currentRoundNumber())) {
    cslog() << "\tpacket sync not finished, saving characteristic meta to call after sync";

    cs::Bytes characteristicBytes(istream_.getCurrentPtr(), istream_.getEndPtr());

    cslog() << "\tsaving characteristic meta with bytes size " << characteristicBytes.size();

    cs::CharacteristicMeta meta;
    meta.bytes = std::move(characteristicBytes);
    meta.sender = sender;

    conveyer.addCharacteristicMeta(conveyer.currentRoundNumber(), std::move(meta));
    // no return, perform some more actions at the end
  }
  else {
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
    }

    cslog() << "\tsequence " << poolMetaInfo.sequenceNumber << ", mask size " << characteristicMask.size();
    csdebug() << "\ttime = " << poolMetaInfo.timestamp;

    cs::Characteristic characteristic;
    characteristic.mask = std::move(characteristicMask);

    assert(sequence <= this->getRoundNumber());
    ////////////////////////////////////////////////////////////////////////////////////////////////////

    conveyer.setCharacteristic(characteristic, cs::numeric_cast<cs::RoundNumber>(poolMetaInfo.sequenceNumber));
    std::optional<csdb::Pool> pool = conveyer.applyCharacteristic(poolMetaInfo, writerPublicKey);

    if (pool.has_value()) {

      if(!getBlockChain().storeBlock(pool.value(), signature)) {
        cserror() << "NODE> failed to store block in BlockChain";
      }
    }
  }

  onRoundStart_V3(roundTable);
#ifdef SYNCRO
  blockchainSync();
#endif
  onRoundStartConveyer(std::move(roundTable));
  // defer until solver_->gotRound() called:
  // transport_->processPostponed(roundNum_);

  cslog() << "NODE> " << __func__ << "(): done\n";
}

void Node::logPool(csdb::Pool& pool) {
  csdebug() << "======== BLOCK DETAILS ========";
  const auto& conf = pool.confidants();
  if (conf.empty()) {
    csdebug() << "    trusted: empty";
  }
  else {
    csdebug() << "    trusted: " << conf.size();
  }
  csdebug() << " prev. hash: " << pool.previous_hash().to_string();
  csdebug() << "       hash: " << pool.hash().to_string();
  csdebug() << " writer key: "
            << cs::Utils::byteStreamToHex(pool.writer_public_key().data(), pool.writer_public_key().size());
  if (pool.transactions().empty()) {
    csdebug() << "      trans: empty";
  }
  else {
    std::ostringstream os;
    int i = 0;
    for (const auto& t : pool.transactions()) {
      os << ' ' << t.innerID();
      ++i;
      if (i == 15) {
        os << "...";
        break;
      }
    }
    csdebug() << "trans. (" << pool.transactions_count() << "):" << os.str();
  }
  csdebug() << "===============================";
}

void Node::sendHash_V3(cs::RoundNumber round) {
  /* if (myLevel_ == NodeLevel::Writer || myLevel_ == NodeLevel::Main) {
     cserror() <<"Writer and Main node shouldn't send hashes";
     return;
   }*/

  const auto& tmp = getBlockChain().getLastWrittenHash();
  // cs::Hash testHash;
  // std::copy(tmp.cbegin(), tmp.cend(), testHash.begin());

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

  cslog() << "NODE> get hash of round " << rNum << ", data size " << size;

  istream_.init(data, size);

  csdb::PoolHash tmp;
  istream_ >> tmp;

  if (!istream_.good() || !istream_.end()) {
    cswarning() << "NODE> bad hash packet format";
    return;
  }

  solver_->gotHash(std::move(tmp), sender);
}

void Node::sendRoundInfoRequest(uint8_t respondent) {
  if (myLevel_ != NodeLevel::Confidant) {
    cswarning() << "NODE> only confidant nodes can request consensus stages";
  }

  cslog() << "NODE> send request for next round info after #" << roundNum_;

  ostream_.init(0 /*need no flags!*/, cs::Conveyer::instance().currentRoundTable().confidants.at(respondent));
  ostream_ << MsgTypes::RoundInfoRequest << roundNum_ << myConfidantIndex_;
  flushCurrentTasks();
}

void Node::getRoundInfoRequest(const uint8_t* data, const size_t size, const cs::RoundNumber rNum,
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
  cslog() << "NODE> get request for next round info after #" << rNum << " from [" << (int)requesterNumber << "]";
  solver_->gotRoundInfoRequest(requester, rNum);
}

void Node::sendRoundInfoReply(const cs::PublicKey& target, bool has_requested_info) {
  cslog() << "NODE> send RoundInfo reply to " << cs::Utils::byteStreamToHex(target.data(), target.size());
  if (myLevel_ != NodeLevel::Confidant) {
    cswarning() << "Only confidant nodes can reply consensus stages";
    // return;
  }

  ostream_.init(0 /*need no flags!*/, target);
  ostream_ << MsgTypes::RoundInfoReply << roundNum_ << (has_requested_info ? (uint8_t)1 : (uint8_t)0);
  flushCurrentTasks();
}

bool Node::tryResendRoundInfo(const cs::PublicKey& respondent, cs::RoundNumber rNum) {
  if (lastSentRoundData_.roundTable.round != rNum) {
    cswarning() << "NODE> unable to repeat round data #" << rNum;
    return false;
  }
  createRoundPackage(lastSentRoundData_.roundTable, lastSentRoundData_.poolMetaInfo, lastSentRoundData_.characteristic,
                     lastSentRoundData_.poolSignature /*, last_sent_round_data.notifications*/);
  flushCurrentTasks();
  cslog() << "NODE> re-send last round info #" << rNum << " to "
          << cs::Utils::byteStreamToHex(respondent.data(), respondent.size());
  return true;
}

void Node::getRoundInfoReply(const uint8_t* data, const size_t size,
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
  roundNum_ = roundTable.round;
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

  constexpr int pad_width = 30;
  int width = 0;
  std::ostringstream line1;
  for (int i = 0; i < pad_width; i++) {
    line1 << '=';
  }
  width += pad_width;
  line1 << " ROUND " << roundNum_ << ". ";
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
  cslog() << line2.str();
  solver_->nextRound();

  if (!sendingTimer_.isRunning()) {
    cslog() << "NODE> Transaction timer started";
    sendingTimer_.start(cs::TransactionsPacketInterval);
  }
}

void Node::startConsensus() {
  cs::RoundNumber rnum = cs::Conveyer::instance().currentRoundNumber();
  solver_->gotRound(rnum);
  transport_->processPostponed(rnum);
  auto lws = getBlockChain().getLastWrittenSequence();
  // claim the trusted role only if have got proper blockchain:
  if (rnum > lws && rnum - lws == 1) {
    sendHash_V3(rnum);
  }
}

void Node::passBlockToSolver(csdb::Pool& pool, const cs::PublicKey& sender) {
  solver_->rndStorageProcessing();
  if (pool.sequence() == getBlockChain().getLastWrittenSequence() + 1) {
    if (getBlockChain().getLastHash() == pool.previous_hash()) {
      solver_->gotBlock(std::move(pool), sender);
    }
    else {
      size_t localSeq = getBlockChain().getLastWrittenSequence();
      size_t blockSeq = pool.sequence();
      cswarning() << "Node: prev. hash of block [" << blockSeq << "] != blockchain last hash of block [" << localSeq
                  << "]";
      cslog() << "Blockchain last hash = " << getBlockChain().getLastHash().to_string();
      cslog() << "Block prev. hash = " << pool.previous_hash().to_string();
      // TODO:: reimplement required
      // getBlockChain().revertLastBlock();
      solver_->gotIncorrectBlock(std::move(pool), sender);
    }
  }
  else {
    solver_->gotIncorrectBlock(std::move(pool), sender);
  }
}
//          A                                          A                                              A
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////                                              SOLVER 3 METHODS (FINISH)                                    ////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
