#include <sstream>

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
  delete transport_;
  delete solver_;
}

bool Node::init() {
  if (!transport_->isGood()) {
    return false;
  }

  if (!bc_.isGood()) {
    return false;
  }

  // Create solver
  if (!solver_) {
    return false;
  }

  csdebug() << "Everything init";

  // check file with keys
  if (!checkKeysFile()) {
    return false;
  }

  cs::PublicKey publicKey;
  std::copy(myPublicKeyForSignature_.begin(), myPublicKeyForSignature_.end(), publicKey.begin());

  cs::PrivateKey privateKey;
  std::copy(myPrivateKeyForSignature_.begin(), myPrivateKeyForSignature_.end(), privateKey.begin());

  solver_->setKeysPair(publicKey, privateKey);
  solver_->runSpammer();

  cs::Connector::connect(&sendingTimer_.timeOut, this, &Node::processTimer);

#ifdef FLUSH_PACKETS_TO_NETWORK
  cs::Connector::connect(cs::Conveyer::instance().flushSignal(), this, &Node::onTransactionsPacketFlushed);
#endif

  return true;
}

bool Node::checkKeysFile() {
  std::ifstream pub("NodePublic.txt");    // 44
  std::ifstream priv("NodePrivate.txt");  // 88

  if (!pub.is_open() || !priv.is_open()) {
    cslog() << "\n\nNo suitable keys were found. Type \"g\" to generate or \"q\" to quit.";

    char gen_flag = 'a';
    std::cin >> gen_flag;

    if (gen_flag == 'g') {
      generateKeys();
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

    DecodeBase58(pub58, myPublicKeyForSignature_);
    DecodeBase58(priv58, myPrivateKeyForSignature_);

    if (myPublicKeyForSignature_.size() != 32 || myPrivateKeyForSignature_.size() != 64) {
      cslog() << "\n\nThe size of keys found is not correct. Type \"g\" to generate or \"q\" to quit.";

      char gen_flag = 'a';
      std::cin >> gen_flag;

      bool needGenerateKeys = gen_flag == 'g';

      if (gen_flag == 'g') {
        generateKeys();
      }

      return needGenerateKeys;
    }

    return checkKeysForSig();
  }
}

void Node::generateKeys() {
  myPublicKeyForSignature_.clear();
  myPrivateKeyForSignature_.clear();

  std::string pub58, priv58;
  pub58  = EncodeBase58(myPublicKeyForSignature_);
  priv58 = EncodeBase58(myPrivateKeyForSignature_);

  myPublicKeyForSignature_.resize(32);
  myPrivateKeyForSignature_.resize(64);

  crypto_sign_keypair(myPublicKeyForSignature_.data(), myPrivateKeyForSignature_.data());

  std::ofstream f_pub("NodePublic.txt");
  f_pub << EncodeBase58(myPublicKeyForSignature_);
  f_pub.close();

  std::ofstream f_priv("NodePrivate.txt");
  f_priv << EncodeBase58(myPrivateKeyForSignature_);
  f_priv.close();
}

bool Node::checkKeysForSig() {
  const uint8_t msg[] = {255, 0, 0, 0, 255};
  uint8_t signature[64], public_key[32], private_key[64];

  for (size_t i = 0; i < 32; i++) {
    public_key[i] = myPublicKeyForSignature_[i];
  }

  for (size_t i = 0; i < 64; i++) {
    private_key[i] = myPrivateKeyForSignature_[i];
  }

  unsigned long long sig_size;
  crypto_sign_detached(signature, &sig_size, msg, 5, private_key);

  int ver_ok = crypto_sign_verify_detached(signature, msg, 5, public_key);

  if (ver_ok == 0) {
    return true;
  }

  cslog() << "\n\nThe keys for node are not correct. Type \"g\" to generate or \"q\" to quit.";

  char gen_flag = 'a';
  std::cin >> gen_flag;

  if (gen_flag == 'g') {
    generateKeys();
    return true;
  }

  return false;
}

void Node::blockchainSync() {
  if (!isSyncroStarted_) {
    if (roundNum_ != getBlockChain().getLastWrittenSequence() + 1) {
      isSyncroStarted_ = true;
      roundToSync_ = roundNum_;

      sendBlockRequest(bc_.getLastWrittenSequence() + 1);
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
    cslog() << __FUNCTION__ << " confidant: " << confidant.data();
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

void Node::sendRoundTableRequest(size_t rNum) {
  if (rNum < roundNum_) {
    return;
  }

  cslog() << "rNum = " << rNum << ", real RoundNumber = " << roundNum_;

  ostream_.init(BaseFlags::Broadcast);
  ostream_ << MsgTypes::RoundTableRequest << roundNum_;

  cslog() << "Sending RoundTable request";

  flushCurrentTasks();
}

void Node::getRoundTableRequest(const uint8_t* data, const size_t size, const cs::PublicKey& sender) {
  istream_.init(data, size);

  size_t rNum = 0u;
  istream_ >> rNum;

  if (rNum >= roundNum_) {
    return;
  }

  cslog() << "NODE> Get RT request from " << cs::Utils::byteStreamToHex(sender.data(), sender.size());

  if (!istream_.good()) {
    cswarning() << "Bad RoundTableRequest format";
    return;
  }

  sendRoundTable(cs::Conveyer::instance().roundTable());
}

template <class... Args>
bool Node::sendDirect(const cs::PublicKey& sender, const MsgTypes& msgType, const cs::RoundNumber round, const Args&... args) {
  cs::Bytes bytes;
  cs::DataStream stream(bytes);

  writeDefaultStream(stream, args...);

  return sendDirect(sender, msgType, round, bytes);
}

bool Node::sendDirect(const cs::PublicKey& sender, const MsgTypes& msgType, const cs::RoundNumber round, const cs::Bytes& bytes) {
#ifdef DIRECT_TRANSACTIONS_REQUEST
  ConnectionPtr connection = transport_->getConnectionByKey(sender);

  if (connection) {
    sendDirect(connection, msgType, round, bytes);
  }

  return connection;
#else
  ostream_.init(BaseFlags::Fragmented | BaseFlags::Compressed, sender);
  ostream_ << msgType << round << bytes;

  csdebug() << "NODE> Sending data Direct: data size " << bytes.size();
  csdebug() << "NODE> Sending data Direct: to " << cs::Utils::byteStreamToHex(sender.data(), sender.size());

  flushCurrentTasks();
  return true;
#endif
}

void Node::sendDirect(const ConnectionPtr& connection, const MsgTypes& msgType, const cs::RoundNumber round, const cs::Bytes& bytes) {
  ostream_.init(BaseFlags::Neighbors | BaseFlags::Broadcast | BaseFlags::Fragmented | BaseFlags::Compressed);
  ostream_ << msgType << round << bytes;

  csdebug() << "NODE> Sending data Direct: data size " << bytes.size();
  csdebug() << "NODE> Sending data Direct: to " << connection->getOut();

  transport_->deliverDirect(ostream_.getPackets(), ostream_.getPacketsCount(), connection);
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
  ostream_ << msgType << round << bytes;

  csdebug() << "NODE> Sending data Broadcast";

#ifdef DIRECT_TRANSACTIONS_REQUEST
  transport_->sendBroadcast(ostream_.getPackets());
  ostream_.clear();
#else
  flushCurrentTasks();
#endif
}

template <class... Args>
bool Node::sendToRandomNeighbour(const MsgTypes& msgType, const cs::RoundNumber round, const Args&... args) {
  cs::Bytes bytes;
  cs::DataStream stream(bytes);

  writeDefaultStream(stream, args...);

  return sendToRandomNeighbour(msgType, round, bytes);
}

bool Node::sendToRandomNeighbour(const MsgTypes& msgType, const cs::RoundNumber round, const cs::Bytes& bytes) {
  ConnectionPtr connection = transport_->getRandomNeighbour();

  if (connection) {
    sendDirect(connection, msgType, round, bytes);
  }

  return connection;
}

void Node::sendVectorRequest(const cs::PublicKey& node) {
  if (myLevel_ != NodeLevel::Confidant) {
    cswarning() << "Only confidant nodes can send vectors";
    return;
  }

  cslog() << "NODE> Sending vector request to  " << cs::Utils::byteStreamToHex(node.data(), node.size());

  ostream_.init(BaseFlags::Signed, node);
  ostream_ << MsgTypes::ConsVectorRequest << roundNum_ << 1;

  flushCurrentTasks();
}

void Node::getVectorRequest(const uint8_t* data, const size_t size) {
  cslog() << __func__;

  if (myLevel_ != NodeLevel::Confidant) {
    return;
  }

  cslog() << "NODE> Getting vector Request from ";  // byteStreamToHex(sender.str, 32) <<

  istream_.init(data, size);

  int num = 0;
  istream_ >> num;

  if (num == 1) {
    sendVector(solver_->getMyVector());
  }
  if (!istream_.good() || !istream_.end()) {
    cswarning() << "Bad vector packet format";
    return;
  }
}

void Node::sendWritingConfirmation(const cs::PublicKey& node) {
  if (myLevel_ != NodeLevel::Confidant) {
    cserror() << "Only confidant nodes can send confirmation of the Writer";
    return;
  }

  cslog() << "NODE> Sending writing confirmation to  " << cs::Utils::byteStreamToHex(node.data(), node.size());

  ostream_.init(BaseFlags::Signed, node);
  ostream_ << MsgTypes::ConsVectorRequest << roundNum_ << getConfidantNumber();

  flushCurrentTasks();
}

void Node::sendMatrixRequest(const cs::PublicKey& node) {
  if (myLevel_ != NodeLevel::Confidant) {
    cserror() << "Only confidant nodes can send vectors";
    return;
  }

  cslog() << "NODE> Sending vector request to  " << cs::Utils::byteStreamToHex(node.data(), node.size());

  ostream_.init(BaseFlags::Signed, node);
  ostream_ << MsgTypes::ConsMatrixRequest << roundNum_ << 1;
  flushCurrentTasks();
}

void Node::getMatrixRequest(const uint8_t* data, const size_t size) {
  if (myLevel_ != NodeLevel::Confidant) {
    return;
  }

  cslog() << "NODE> Getting matrix Request";

  istream_.init(data, size);

  int num;
  istream_ >> num;

  if (!istream_.good() || !istream_.end()) {
    LOG_ERROR("Bad vector packet format");
    return;
  }

  if (num == 1) {
    sendMatrix(solver_->getMyMatrix());
  }
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

void Node::getBlock(const uint8_t* data, const size_t size, const cs::PublicKey& sender) {
  if (myLevel_ == NodeLevel::Writer) {
    cswarning() << "Writer cannot get blocks";
    return;
  }

  csunused(sender);

  istream_.init(data, size);

  csdb::Pool pool;
  istream_ >> pool;

  if (!istream_.good() || !istream_.end()) {
    cswarning() << "Bad block packet format";
    return;
  }

  size_t localSeq = getBlockChain().getLastWrittenSequence();
  size_t blockSeq = pool.sequence();

  if (roundNum_ == blockSeq) {
    getBlockChain().setGlobalSequence(cs::numeric_cast<uint32_t>(blockSeq));
  }

  if (localSeq >= blockSeq) {
    return;
  }

  if (!blocksReceivingStarted_) {
    blocksReceivingStarted_ = true;
    lastStartSequence_      = pool.sequence();
    csdebug() << "GETBLOCK> Setting first got block: " << lastStartSequence_;
  }

  if (pool.sequence() == getBlockChain().getLastWrittenSequence() + 1) {
    solver_->gotBlock(std::move(pool), sender);
  }
  else {
    solver_->gotIncorrectBlock(std::move(pool), sender);
  }
}

void Node::sendBlock(const csdb::Pool& pool) {
  if (myLevel_ != NodeLevel::Writer) {
    cserror() << "Only writer nodes can send blocks";
    return;
  }

  ostream_.init(BaseFlags::Broadcast | BaseFlags::Fragmented | BaseFlags::Compressed);
  composeMessageWithBlock(pool, MsgTypes::NewBlock);

  csdebug() << "Sending block of " << pool.transactions_count() << " transactions of seq " << pool.sequence()
            << " and hash " << pool.hash().to_string() << " and ts " << pool.user_field(0).value<std::string>();

  flushCurrentTasks();
}

void Node::getBadBlock(const uint8_t* data, const size_t size, const cs::PublicKey& sender) {
  if (myLevel_ == NodeLevel::Writer) {
    cswarning() << "Writer cannot get bad blocks";
    return;
  }

  istream_.init(data, size);

  csdb::Pool pool;
  istream_ >> pool;

  if (!istream_.good() || !istream_.end()) {
    cswarning() << "Bad block packet format";
    return;
  }

  cslog() << "Got block of " << pool.transactions_count() << " transactions";

  solver_->gotBadBlockHandler(std::move(pool), sender);
}

void Node::sendBadBlock(const csdb::Pool& pool) {
  if (myLevel_ != NodeLevel::Writer) {
    cserror() << "Only writer nodes can send bad blocks";
    return;
  }

  ostream_.init(BaseFlags::Broadcast | BaseFlags::Fragmented | BaseFlags::Compressed);
  composeMessageWithBlock(pool, MsgTypes::NewBadBlock);
}

void Node::getHash(const uint8_t* data, const size_t size, const cs::PublicKey& sender) {
  if (myLevel_ != NodeLevel::Writer) {
    return;
  }

  cslog() << "Get hash size: " << size;

  istream_.init(data, size);

  std::string hash;
  istream_ >> hash;

  if (!istream_.good() || !istream_.end()) {
    cswarning() << "Bad hash packet format";
    return;
  }

  solver_->gotHash(std::move(hash), sender);
}

void Node::getTransactionsPacket(const uint8_t* data, const std::size_t size) {
  istream_.init(data, size);

  cs::Bytes bytes;
  istream_ >> bytes;

  cs::TransactionsPacket packet = cs::TransactionsPacket::fromBinary(bytes);

  csdebug() << "NODE> Transactions amount got " << packet.transactionsCount();

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
    csdebug() << "NODE> Sync packets already synced";
    return;
  }

  cs::DataStream stream(data, size);

  std::size_t packetsCount = 0;
  stream >> packetsCount;

  std::vector<cs::TransactionsPacket> packets;
  packets.reserve(packetsCount);

  for (std::size_t i = 0; i < packetsCount; ++i) {
    cs::TransactionsPacket packet;
    stream >> packet;

    if (!packet.transactions().empty()) {
      packets.push_back(packet);
    }
  }

  if (packets.size() != packetsCount) {
    cserror() << "NODE> Packet hashes reply, bad packets parsing";
    return;
  }

  csdebug() << "NODE> Get transactions sender " << cs::Utils::byteStreamToHex(sender.data(), sender.size());

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

void Node::writeBlock(csdb::Pool newPool, size_t sequence, const cs::PublicKey& sender) {
  csdebug() << "GOT NEW BLOCK: global sequence = " << sequence;

  if (sequence > this->getRoundNumber()) {
    return;  // remove this line when the block candidate signing of all trusted will be implemented
  }

  this->getBlockChain().setGlobalSequence(cs::numeric_cast<uint32_t>(sequence));

#ifndef MONITOR_NODE
  if (sequence == (this->getBlockChain().getLastWrittenSequence() + 1)) {
    this->getBlockChain().putBlock(newPool);

    if ((this->getNodeLevel() != NodeLevel::Writer) && (this->getNodeLevel() != NodeLevel::Main)) {
      auto hash = this->getBlockChain().getLastWrittenHash().to_string();

      this->sendHash(hash, sender);

      cslog() << "SENDING HASH to writer: " << hash;
    } else {
      cslog() << "I'm node " << this->getNodeLevel() << " and do not send hash";
    }
  }
  else {
    solver_->gotIncorrectBlock(std::move(newPool), sender);
  }
#else
  if (sequence == (this->getBlockChain().getLastWrittenSequence() + 1)) {
    this->getBlockChain().putBlock(newPool);
  } else {
    solver_->gotIncorrectBlock(std::move(newPool), sender);
  }
#endif
}

void Node::getWriterNotification(const uint8_t* data, const std::size_t size, const cs::PublicKey& senderPublicKey) {
  if (!isCorrectNotification(data, size)) {
    cswarning() << "NODE> Notification failed " << cs::Utils::byteStreamToHex(senderPublicKey.data(), senderPublicKey.size());
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

  pool.value().sign(myPrivateKeyForSignature_);

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

void Node::sendHash(const std::string& hash, const cs::PublicKey& target) {
  if (myLevel_ == NodeLevel::Writer || myLevel_ == NodeLevel::Main) {
    cserror() << "Writer and Main node shouldn't send hashes";
    return;
  }

  cswarning() << "Sending hash of " << roundNum_ << " to " << cs::Utils::byteStreamToHex(target.data(), target.size());
  cslog() << "Hash is " << hash;

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
  ostream_ << MsgTypes::TransactionPacket << roundNum_ << packet.toBinary();

  flushCurrentTasks();
}

void Node::sendPacketHashesRequest(const std::vector<cs::TransactionsPacketHash>& hashes, const cs::RoundNumber round) {
  if (myLevel_ == NodeLevel::Writer) {
    cserror() << "Writer should has all transactions hashes";
    return;
  }

  cs::Bytes bytes;
  cs::DataStream stream(bytes);

  stream << hashes.size();

  for (const auto& hash : hashes) {
    stream << hash;
    csdebug() << "NODE> Sending transaction packet request: need hash - " << hash.toString();
  }

  ostream_.init(BaseFlags::Broadcast | BaseFlags::Fragmented | BaseFlags::Compressed);
  ostream_ << MsgTypes::TransactionsPacketRequest << round << bytes;

  flushCurrentTasks();
}

void Node::sendPacketHashesReply(const std::vector<cs::TransactionsPacket>& packets, const cs::RoundNumber round, const cs::PublicKey& sender) {
  if (packets.empty()) {
    return;
  }

  cs::Bytes bytes;
  cs::DataStream stream(bytes);

  stream << packets.size();

  for (const auto& packet : packets) {
    stream << packet;
  }

  ostream_.init(BaseFlags::Fragmented | BaseFlags::Compressed, sender);
  ostream_ << MsgTypes::TransactionsPacketReply << round << bytes;

  cslog() << "Sending reply with " << packets.size() << " count of packs to " << cs::Utils::byteStreamToHex(sender.data(), sender.size());

  flushCurrentTasks();
}

void Node::resetNeighbours() {
  transport_->resetNeighbours();
}

void Node::getBlockRequest(const uint8_t* data, const size_t size, const cs::PublicKey& sender) {
  uint32_t requested_seq = 0u;

  istream_.init(data, size);
  istream_ >> requested_seq;

  cslog() << "GETBLOCKREQUEST> Getting the request for block: " << requested_seq;

  if (requested_seq > getBlockChain().getLastWrittenSequence()) {
    cslog() << "GETBLOCKREQUEST> The requested block: " << requested_seq << " is BEYOND my CHAIN";
    return;
  }

  solver_->gotBlockRequest(getBlockChain().getHashBySequence(requested_seq), sender);
}

void Node::sendBlockRequest(uint32_t seq) {
  static uint32_t lfReq, lfTimes;

  seq = getBlockChain().getLastWrittenSequence() + 1;

  csdb::Pool::sequence_t lws = getBlockChain().getLastWrittenSequence();
  csdb::Pool::sequence_t gs = getBlockChain().getGlobalSequence();

  if (gs == 0) {
    gs = roundNum_;
  }

  csdb::Pool::sequence_t cached = solver_->getCountCahchedBlock(lws, gs);
  const uint32_t syncStatus = cs::numeric_cast<int>((1.0f - (gs * 1.0f - lws * 1.0f - cached * 1.0f) / gs) * 100.0f);
  std::stringstream progress;

  if (syncStatus <= 100) {
    progress << "SYNC: [";
    for (uint32_t i = 0; i < syncStatus; ++i) if (i % 2) progress << "#";
    for (uint32_t i = syncStatus; i < 100; ++i) if (i % 2) progress << "-";
    progress << "] " << syncStatus << "%";
  }

  cslog() << progress.str();

  uint32_t reqSeq = seq;

  if (lfReq != seq) {
    lfReq = seq;
    lfTimes = 0;
  }

  while (reqSeq) {
    bool alreadyRequested = false;
    ConnectionPtr requestee = transport_->getSyncRequestee(reqSeq, alreadyRequested);
    if (!requestee) {
      break;  // No more free requestees
    }

    if (!alreadyRequested) {  // Already requested this block from this guy?
      cslog() << "Sending request for block " << reqSeq << " from nbr " << requestee->id;

      ostream_.init(BaseFlags::Neighbors | BaseFlags::Signed);
      ostream_ << MsgTypes::BlockRequest << roundNum_ << reqSeq;

      transport_->deliverDirect(ostream_.getPackets(), ostream_.getPacketsCount(), requestee);

      if (lfReq == reqSeq && ++lfTimes >= 4) {
        transport_->sendBroadcast(ostream_.getPackets());
      }

      ostream_.clear();
    }

    reqSeq = cs::numeric_cast<uint32_t>(solver_->getNextMissingBlock(reqSeq));
  }

  //#endif
  sendBlockRequestSequence_ = seq;
  isAwaitingSyncroBlock_ = true;
  awaitingRecBlockCount_ = 0;

  csdebug() << "SEND BLOCK REQUEST> Sending request for block: " << seq;
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

    if (pool.sequence() == bc_.getLastWrittenSequence() + 1) {
      bc_.onBlockReceived(pool);
    }

    isAwaitingSyncroBlock_ = false;
  }
  else {
    solver_->gotFreeSyncroBlock(std::move(pool));
  }

  if (roundToSync_ != bc_.getLastWrittenSequence()) {
    sendBlockRequest(getBlockChain().getLastWrittenSequence() + 1);
  } else {
    isSyncroStarted_ = false;
    roundToSync_ = 0;

    processMetaMap();
    cslog() << "SYNCRO FINISHED!!!";
  }
}

void Node::sendBlockReply(const csdb::Pool& pool, const cs::PublicKey& sender) {
  ConnectionPtr conn = transport_->getConnectionByKey(sender);
  if (!conn) {
    LOG_WARN("Cannot get a connection with a specified public key");
    return;
  }

  ostream_.init(BaseFlags::Neighbors | BaseFlags::Broadcast | BaseFlags::Fragmented | BaseFlags::Compressed);
  composeMessageWithBlock(pool, MsgTypes::RequestedBlock);
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
  std::vector<cs::TransactionsPacket> packets;

  for (const auto& hash : hashes) {
    std::optional<cs::TransactionsPacket> packet = cs::Conveyer::instance().searchPacket(hash, round);

    if (packet) {
      packets.push_back(std::move(packet).value());
    }
  }

  sendPacketHashesReply(packets, round, sender);
}

void Node::processPacketsReply(std::vector<cs::TransactionsPacket>&& packets, const cs::RoundNumber round) {
  csdebug() << "NODE> Processing packets reply";

  cs::Conveyer& conveyer = cs::Conveyer::instance();

  for (auto&& packet : packets) {
    conveyer.addFoundPacket(round, std::move(packet));
  }

  if (conveyer.isSyncCompleted(round)) {
    csdebug() << "NODE> Packets sync completed";
    solver_->gotRound();

    if (auto meta = conveyer.characteristicMeta(round); meta.has_value())
    {
      csdebug() << "NODE> Run characteristic meta";
      getCharacteristic(meta->bytes.data(), meta->bytes.size(), round, meta->sender);
    }
  }
}

void Node::processTransactionsPacket(cs::TransactionsPacket&& packet)
{
  csdebug() << "NODE> Process transaction packet";
  cs::Conveyer::instance().addTransactionsPacket(packet);
}

void Node::onRoundStartConveyer(cs::RoundTable&& roundTable) {
  cs::Conveyer& conveyer = cs::Conveyer::instance();
  conveyer.setRound(std::move(roundTable));

  if (conveyer.isSyncCompleted()) {
    cslog() << "NODE> All hashes in conveyer";
  }
  else {
    csdebug() << "NODE> Sending packet hashes request";
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

void Node::composeMessageWithBlock(const csdb::Pool& pool, const MsgTypes type) {
  uint32_t bSize;
  const void* data = const_cast<csdb::Pool&>(pool).to_byte_stream(bSize);
  composeCompressed(data, bSize, type);
}

void Node::composeCompressed(const void* data, const uint32_t bSize, const MsgTypes type) {
  auto max    = LZ4_compressBound(cs::numeric_cast<int>(bSize));
  auto memPtr = allocator_.allocateNext(cs::numeric_cast<uint32_t>(max));

  auto realSize = LZ4_compress_default((const char*)data, (char*)memPtr.get(), cs::numeric_cast<int>(bSize), cs::numeric_cast<int>(memPtr.size()));

  allocator_.shrinkLast(cs::numeric_cast<uint32_t>(realSize));
  ostream_ << type << roundNum_ << bSize;
  ostream_ << std::string(cs::numeric_cast<char*>(memPtr.get()), memPtr.size());
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
