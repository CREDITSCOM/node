#include <solver/solver.hpp>

#include <csnode/nodecore.h>
#include <csnode/node.hpp>

#include <lib/system/logger.hpp>
#include <lib/system/utils.hpp>

#include <net/transport.hpp>

#include <base58.h>

#include <lz4.h>
#include <sodium.h>

#include <snappy.h>
#include <sodium.h>

const unsigned MIN_CONFIDANTS = 3;
const unsigned MAX_CONFIDANTS = 100;

Node::Node(const Config& config)
: myPublicKey_(config.getMyPublicKey())
, bc_(config.getPathToDB().c_str())
, solver_(new cs::Solver(this))
, transport_(new Transport(config, this))
#ifdef MONITOR_NODE
, stats_(bc_)
#endif
#ifdef NODE_API
, api_(bc_, solver_)
#endif
, packStreamAllocator_(1 << 26, 5)
, allocator_(1 << 26, 3)
, ostream_(&packStreamAllocator_, myPublicKey_) {
  good_ = init();
}

Node::~Node() {
  delete transport_;
  delete solver_;
}

bool Node::init() {
  if (!transport_->isGood())
    return false;

  if (!bc_.isGood())
    return false;

  // Create solver
  if (!solver_)
    return false;

  csdebug() << "Everything init";

  // check file with keys
  if (!checkKeysFile())
    return false;

  cs::PublicKey publicKey; 
  std::copy(myPublicForSig.begin(), myPublicForSig.end(), publicKey.begin());

  cs::PrivateKey privateKey;
  std::copy(myPrivateForSig.begin(), myPrivateForSig.end(), privateKey.begin());

  solver_->setKeysPair(publicKey, privateKey);
  //solver_->addInitialBalance();

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

    DecodeBase58(pub58, myPublicForSig);
    DecodeBase58(priv58, myPrivateForSig);

    if (myPublicForSig.size() != 32 || myPrivateForSig.size() != 64) {
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
  myPublicForSig.clear();
  myPrivateForSig.clear();

  std::string pub58, priv58;
  pub58  = EncodeBase58(myPublicForSig);
  priv58 = EncodeBase58(myPrivateForSig);

  myPublicForSig.resize(32);
  myPrivateForSig.resize(64);

  crypto_sign_keypair(myPublicForSig.data(), myPrivateForSig.data());

  std::ofstream f_pub("NodePublic.txt");
  f_pub << EncodeBase58(myPublicForSig);
  f_pub.close();

  std::ofstream f_priv("NodePrivate.txt");
  f_priv << EncodeBase58(myPrivateForSig);
  f_priv.close();
}

bool Node::checkKeysForSig() {
  const uint8_t msg[] = {255, 0, 0, 0, 255};
  uint8_t signature[64], public_key[32], private_key[64];

  for (int i = 0; i < 32; i++) {
    public_key[i] = myPublicForSig[i];
  }

  for (int i = 0; i < 64; i++) {
    private_key[i] = myPrivateForSig[i];
  }

  uint64_t sig_size;
  crypto_sign_detached(signature, &sig_size, msg, 5, private_key);

  int ver_ok = crypto_sign_verify_detached(signature, msg, 5, public_key);

  if (ver_ok == 0) {
    return true;
  }
  else {
    cslog() << "\n\nThe keys for node are not correct. Type \"g\" to generate or \"q\" to quit.";

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
}

void Node::run(const Config&) {
  transport_->run();
}

/* Requests */
void Node::flushCurrentTasks() {
  transport_->addTask(ostream_.getPackets(), ostream_.getPacketsCount());
  ostream_.clear();
}

void Node::getRoundTableSS(const uint8_t* data, const size_t size, const RoundNum rNum, uint8_t type) {
  istream_.init(data, size);

  cslog() << "NODE> Get Round Table";

  if (roundNum_ < rNum || type == MsgTypes::BigBang) {
    roundNum_ = rNum;
  } else {
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

  // TODO: think how to improve this code
  cs::Utils::runAfter(std::chrono::milliseconds(TIME_TO_AWAIT_ACTIVITY), [this, roundTable]() mutable {

      transport_->clearTasks();

      onRoundStart(roundTable);
      solver_->gotRound(std::move(roundTable));

  });
}

void Node::getBigBang(const uint8_t* data, const size_t size, const RoundNum rNum, uint8_t type) {
  uint32_t lastBlock = getBlockChain().getLastWrittenSequence();

  if (rNum > lastBlock && rNum >= roundNum_) {
    getRoundTableSS(data, size, rNum, type);
    solver_->setBigBangStatus(true);
  } else {
    cslog() << "BigBang else";
  }
}

void Node::sendRoundTable(const cs::RoundTable& roundTable) {
  ostream_.init(BaseFlags::Broadcast);
  ostream_ << MsgTypes::RoundTable;

  cs::Bytes bytes;
  cs::DataStream stream(bytes);

  stream << roundTable.round;
  stream << roundTable.confidants.size();
  stream << roundTable.hashes.size();
  stream << roundTable.general;

  for (const auto& it : roundTable.confidants) {
    stream << it;
  }

  for (const auto& it : roundTable.hashes) {
    stream << it;
  }

  cslog() << "------------------------------------------  SendRoundTable  ---------------------------------------";
  cslog() << "Round " << roundNum_ << ", General: " << cs::Utils::byteStreamToHex(roundTable.general.data(), roundTable.general.size()) << "Confidants: ";

  size_t i = 0;

  for (auto& e : roundTable.confidants) {
    if (e != roundTable.general) {
      cslog() << i << ". " << cs::Utils::byteStreamToHex(e.data(), e.size());
      ++i;
    }
  }

  i = 0;

  cslog() << "Hashes";

  for (auto& e : roundTable.hashes) {
    cslog() << i << ". " << e.toString().c_str();
    i++;
  }

  ostream_ << bytes;

  transport_->clearTasks();
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

  size_t rNum;
  istream_ >> rNum;

  if (rNum >= roundNum_) {
    return;
  }

  cslog() << "NODE> Get RT request from " << cs::Utils::byteStreamToHex(sender.data(), sender.size());

  if (!istream_.good()) {
    cswarning() << "Bad RoundTableRequest format";
    return;
  }

  sendRoundTable(solver_->roundTable());
}

void Node::getTransaction(const uint8_t* data, const size_t size) {
  if (solver_->getIPoolClosed()) {
    return;
  }

  if (myLevel_ != NodeLevel::Main && myLevel_ != NodeLevel::Writer) {
    return;
  }

  istream_.init(data, size);

  csdb::Pool pool;
  istream_ >> pool;

  cslog() << "NODE> Transactions amount got " << pool.transactions_count();

  if (!istream_.good() || !istream_.end()) {
    cswarning() << "Bad transactions packet format";
    return;
  }

  auto& trx = pool.transactions();
  uint16_t i   = 0;

  for (auto& tr : trx) {
    cslog() << "NODE> Get transaction #:" << i << " from " << tr.source().to_string() << " ID= " << tr.innerID();

    solver_->gotTransaction(std::move(tr));
    i++;
  }
}

void Node::getFirstTransaction(const uint8_t* data, const size_t size) {
  if (myLevel_ != NodeLevel::Confidant) {
    return;
  }

  istream_.init(data, size);

  csdb::Transaction trans;
  istream_ >> trans;

  if (!istream_.good() || !istream_.end()) {
    cswarning() << "Bad transaction packet format";
    return;
  }

  csdb::Pool pool_;
  pool_.add_transaction(trans);

  cslog() << "Got first transaction, initializing consensus...";
}

void Node::sendFirstTransaction(const csdb::Transaction& trans) {
  if (myLevel_ != NodeLevel::Main) {
    cserror() << "Only main nodes can initialize the consensus procedure";
    return;
  }

  ostream_.init(BaseFlags::Broadcast);
  ostream_ << MsgTypes::FirstTransaction << roundNum_ << trans;

  flushCurrentTasks();
}

void Node::getTransactionsList(const uint8_t* data, const size_t size) {
  if (myLevel_ != NodeLevel::Confidant) {
    return;
  }

  csdb::Pool pool;
  pool = csdb::Pool{};

  cslog() << "Getting List: list size: " << size;

  if (!((size == 0) || (size > 2000000000))) {
    istream_.init(data, size);
    istream_ >> pool;

    if (!istream_.good() || !istream_.end()) {
      cswarning() << "Bad transactions list packet format";
      pool = csdb::Pool{};
    }

    cslog() << "Got full transactions list of " << pool.transactions_count();
  }
}

void Node::sendTransactionList(const csdb::Pool& pool) {  //, const PublicKey& target) {
  if ((myLevel_ == NodeLevel::Confidant) || (myLevel_ == NodeLevel::Normal)) {
    LOG_ERROR("Only main nodes can send transaction lists");
    return;
  }

  ostream_.init(BaseFlags::Fragmented | BaseFlags::Compressed | BaseFlags::Broadcast);
  composeMessageWithBlock(pool, MsgTypes::TransactionList);

  flushCurrentTasks();
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

  int num;
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
  ostream_ << MsgTypes::ConsVectorRequest << roundNum_ << getMyConfNumber();

  flushCurrentTasks();
}

void Node::getWritingConfirmation(const uint8_t* data, const size_t size, const cs::PublicKey& sender) {
  if (myLevel_ != NodeLevel::Confidant) {
    return;
  }

  cslog() << "NODE> Getting WRITING CONFIRMATION from " << cs::Utils::byteStreamToHex(sender.data(), sender.size());

  istream_.init(data, size);

  uint8_t confNumber_;
  istream_ >> confNumber_;

  if (!istream_.good() || !istream_.end()) {
    cswarning() << "Bad vector packet format";
    return;
  }

  if (confNumber_ < 3) {
    solver_->addConfirmation(confNumber_);
  }
}

void Node::sendTLRequest() {
  if ((myLevel_ != NodeLevel::Confidant) || (roundNum_ < 2)) {
    cserror() << "Only confidant nodes need TransactionList";
    return;
  }

  const auto& mainNode = solver_->roundTable().general;

  cslog() << "NODE> Sending TransactionList request to  " << cs::Utils::byteStreamToHex(mainNode.data(), mainNode.size());

  ostream_.init(BaseFlags::Signed, mainNode);
  ostream_ << MsgTypes::ConsTLRequest << getMyConfNumber();

  flushCurrentTasks();
}

void Node::getTlRequest(const uint8_t* data, const size_t size) {
  if (myLevel_ != NodeLevel::Main) {
    cserror() << "Only main nodes can send TransactionList";
    return;
  }

  cslog() << "NODE> Getting TransactionList request";

  istream_.init(data, size);

  uint8_t num;
  istream_ >> num;

  if (!istream_.good() || !istream_.end()) {
    return;
  }

  if (num < solver_->roundTable().confidants.size()) {
    sendMatrix(solver_->getMyMatrix());
  }
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
    LOG_WARN("Bad vector packet format");
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

  ostream_.init(BaseFlags::Broadcast | BaseFlags::Fragmented | BaseFlags::Compressed);
  ostream_ << MsgTypes::ConsVector << roundNum_;

  cs::Bytes bytes;
  cs::DataStream stream(bytes);

  stream << vector;

  ostream_ << bytes;

  flushCurrentTasks();
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

  cslog() << "NODE> 1 Sending matrix to ";

  ostream_.init(BaseFlags::Broadcast | BaseFlags::Fragmented);
  ostream_ << MsgTypes::ConsMatrix << roundNum_;

  cs::Bytes bytes;
  cs::DataStream stream(bytes);

  stream << matrix;

  ostream_ << bytes;

  flushCurrentTasks();
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
    getBlockChain().setGlobalSequence(blockSeq);
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

  cslog() << "NODE> Transactions amount got " << packet.transactionsCount();

  if (!istream_.good() || !istream_.end()) {
    cswarning() << "Bad transactions packet format";
    return;
  }

  if (packet.hash().isEmpty()) {
    cswarning() << "Received transaction packet hash is empty";
    return;
  }

  solver_->gotTransactionsPacket(std::move(packet));
}

void Node::getPacketHashesRequest(const uint8_t* data, const std::size_t size, const cs::PublicKey& sender) {
  cslog() << "NODE> getPacketHashesReques ";

  cs::DataStream stream(data, size);

  uint32_t hashesCount = 0;
  stream >> hashesCount;

  cs::Hashes hashes;
  hashes.reserve(hashesCount);

  for (std::size_t i = 0; i < hashesCount; ++i) {
    cs::TransactionsPacketHash hash;
    stream >> hash;

    hashes.push_back(std::move(hash));
  }

  cslog() << "NODE> Hashes request got size: " << hashesCount;

  if (hashesCount != hashes.size()) {
    cserror() << "Bad hashes created";
    return;
  }

  solver_->gotPacketHashesRequest(std::move(hashes), sender);
}

void Node::getPacketHashesReply(const uint8_t* data, const std::size_t size) {
  cs::DataStream stream(data, size);

  cs::TransactionsPacket packet;
  stream >> packet;

  cslog() << "NODE> Transactions hashes answer amount got " << packet.transactionsCount();

  if (packet.hash().isEmpty()) {
    cserror() << "Received transaction hashes answer packet hash is empty";
    return;
  }

  solver_->gotPacketHashesReply(std::move(packet));
}

void Node::getRoundTable(const uint8_t* data, const size_t size, const RoundNum round) {
  cslog() << "NODE> RoundTableUpdated";

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

    hashes.push_back(std::move(hash));
  }

  roundTable.general = std::move(general);
  roundTable.confidants = std::move(confidants);
  roundTable.hashes = std::move(hashes);

  onRoundStart(roundTable);

  solver_->gotRound(std::move(roundTable));
}

void Node::sendCharacteristic(const cs::PoolMetaInfo& poolMetaInfo, const cs::Characteristic& characteristic) {
  if (myLevel_ != NodeLevel::Writer) {
    cserror() << "Only writer nodes can send blocks";
    return;
  }

  cslog() << "Send Characteristic: seq = " << poolMetaInfo.sequenceNumber;

  ostream_.init(BaseFlags::Broadcast | BaseFlags::Fragmented);
  ostream_ << MsgTypes::NewCharacteristic << roundNum_;

  cs::Bytes bytes;
  cs::DataStream stream(bytes);

  std::string time = poolMetaInfo.timestamp;
  uint64_t sequence = poolMetaInfo.sequenceNumber;

  stream << time;

  stream << characteristic.size;
  stream << characteristic.mask << sequence;

  ostream_ << bytes;

  flushCurrentTasks();

  cslog() << "Send Characteristic: DONE size: " << stream.size();
}

void Node::getCharacteristic(const uint8_t* data, const size_t size, const cs::PublicKey& sender) {
  cslog() << "Characteric has arrived";

  cs::DataStream stream(data, size);

  std::string time;
  uint32_t maskBitsCount;
  std::vector<uint8_t> characteristicMask;
  uint64_t sequence = 0;

  cslog() << "Characteristic data size: " << size;

  stream >> time >> maskBitsCount;
  stream >> characteristicMask >> sequence;

  cs::PoolMetaInfo poolMetaInfo;
  poolMetaInfo.sequenceNumber = sequence;
  poolMetaInfo.timestamp = time;

  cs::Signature signature;
  stream >> signature;

  std::size_t notificationsSize;
  stream >> notificationsSize;

  if (notificationsSize == 0) {
    cserror() << "Get characteristic: notifications count is zero";
  }

  for (std::size_t i = 0; i < notificationsSize; ++i) {
    cs::Bytes notification;
    stream >> notification;

    m_notifications.push_back(notification);
  }

  std::vector<cs::Hash> confidantsHashes;

  for (const auto& notification : m_notifications) {
    cs::Hash hash;
    cs::DataStream stream(notification.data(), notification.size());

    stream >> hash;

    confidantsHashes.push_back(hash);
  }

  cs::Hash characteristicHash = getBlake2Hash(characteristicMask.data(), characteristicMask.size());

  for (const auto& hash : confidantsHashes) {
    if (hash != characteristicHash) {
      cserror() << "Some of confidants hashes is dirty";
      return;
    }
  }

  const cs::RoundTable& roundTable = solver_->roundTable();

  //for (const auto& confidant : roundTable.confidants) {
  //  if (!cs::Utils::verifySignature(signature, confidant, data, size)) {
  //    cswarning() << "Confidants signatures verification failed";
  //    return;
  //  }
  //}

  cslog() << "GetCharacteristic " << poolMetaInfo.sequenceNumber << " maskbitCount" << maskBitsCount;
  cslog() << "Time >> " << poolMetaInfo.timestamp << "  << Time";

  cs::Characteristic characteristic;
  characteristic.mask = characteristicMask;
  characteristic.size = maskBitsCount;

  solver_->applyCharacteristic(characteristic, poolMetaInfo, sender);
}

void Node::getNotification(const uint8_t* data, const std::size_t size, const cs::PublicKey& senderPublicKey) {
  if (!isCorrectNotification(data, size, senderPublicKey)) {
    cswarning() << "Notification failed " << cs::Utils::byteStreamToHex(senderPublicKey.data(), senderPublicKey.size());
    return;
  }

  m_notifications.emplace_back(data, data + size);

  const std::size_t neededConfidantsCount = (solver_->roundTable().confidants.size() / 2) + 1;

  if (m_notifications.size() < neededConfidantsCount) {
    return;
  }

  cslog() << "Confidants count more then 51%";

  cs::PoolMetaInfo poolMetaInfo;
  poolMetaInfo.sequenceNumber = 1 + bc_.getLastWrittenSequence();
  poolMetaInfo.timestamp = cs::Utils::currentTimestamp();

  const cs::Characteristic& characteristic = solver_->getCharacteristic();
  solver_->applyCharacteristic(characteristic, poolMetaInfo, senderPublicKey);

  // loading block from blockhain, because write pool to blockchain do something with pool
  const cs::Bytes poolBinary = bc_.loadBlock(bc_.getLastHash()).to_binary();
  const cs::Signature poolSignature = cs::Utils::sign(poolBinary, solver_->getPrivateKey());

  ostream_.init(BaseFlags::Broadcast | BaseFlags::Compressed | BaseFlags::Fragmented);
  ostream_ << MsgTypes::NewCharacteristic << roundNum_;
  ostream_ << createBlockValidatingPacket(poolMetaInfo, characteristic, poolSignature, m_notifications);

  flushCurrentTasks();
}

bool Node::isCorrectNotification(const uint8_t* data, const std::size_t size, const cs::PublicKey& senderPublicKey) {
  cs::DataStream stream(data, size);

  cs::Hash characteristicHash;
  stream >> characteristicHash;

  if (characteristicHash != solver_->getCharacteristicHash()) {
    return false;
  }

  cs::PublicKey writerPublicKey;
  stream >> writerPublicKey;

  if (writerPublicKey != myPublicKey_) {
    return false;
  }

  cs::Signature signature;
  stream >> signature;

  cs::PublicKey publicKey;
  stream >> publicKey;

  size_t messageSize = size - signature.size() - publicKey.size();

  if (!cs::Utils::verifySignature(signature, publicKey, data, messageSize)) {
    cserror() << "Data: " << cs::Utils::byteStreamToHex(data, messageSize) << " verification failed";
    csinfo() << "Signature: " << cs::Utils::byteStreamToHex(signature.data(), signature.size());
    return false;
  }

  return true;
}

cs::Bytes Node::createBlockValidatingPacket(const cs::PoolMetaInfo& poolMetaInfo,
                                            const cs::Characteristic& characteristic,
                                            const cs::Signature& signature,
                                            const std::vector<cs::Bytes>& notifications) {
  cs::Bytes bytes;
  cs::DataStream stream(bytes);

  std::string time = poolMetaInfo.timestamp;
  uint64_t sequence = poolMetaInfo.sequenceNumber;

  stream << time;
  stream << characteristic.size;
  stream << characteristic.mask;
  stream << sequence;

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

  cslog() << "Notification sent to writer";

  flushCurrentTasks();
}

cs::Bytes Node::createNotification() {
  cs::Hash characteristicHash = solver_->getCharacteristicHash();
  cs::PublicKey writerPublicKey = solver_->getWriterPublicKey();

  cs::Bytes bytes;
  cs::DataStream stream(bytes);

  stream << characteristicHash << writerPublicKey;

  cs::Signature signature = cs::Utils::sign(bytes, solver_->getPrivateKey());

  if (!cs::Utils::verifySignature(signature, solver_->getPublicKey(), bytes.data(), bytes.size())) {
    cserror() << "Verification failed";
  }

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
  if (myLevel_ != NodeLevel::Normal) {
    return;
  }

  if (packet.hash().isEmpty()) {
    cslog() << "Send transaction packet with empty hash failed";
    return;
  }

  ostream_.init(BaseFlags::Compressed | BaseFlags::Fragmented | BaseFlags::Broadcast);
  ostream_ << MsgTypes::TransactionPacket << roundNum_ << packet.toBinary();

  flushCurrentTasks();
}

void Node::sendPacketHashesRequest(const std::vector<cs::TransactionsPacketHash>& hashes) {
  if (myLevel_ == NodeLevel::Writer) {
    cserror() << "Writer should has all transactions hashes";
    return;
  }

  ostream_.init(BaseFlags::Fragmented | BaseFlags::Compressed | BaseFlags::Broadcast);

  cs::Bytes bytes;
  cs::DataStream stream(bytes);

  stream << static_cast<uint32_t>(hashes.size());

  for (const auto& hash : hashes) {
    stream << hash;
  }

  cslog() << "Sending transaction packet request: size: " << bytes.size();

  ostream_ << MsgTypes::TransactionsPacketRequest << bytes;

  flushCurrentTasks();
}

void Node::sendPacketHashesReply(const cs::TransactionsPacket& packet, const cs::PublicKey& sender) {
  if (packet.hash().isEmpty()) {
    cslog() << "Send transaction packet reply with empty hash failed";
    return;
  }

  ostream_.init(BaseFlags::Fragmented | BaseFlags::Compressed, sender);

  cs::Bytes bytes = packet.toBinary();

  cslog() << "Sending transaction packet reply: size: " << bytes.size();

  ostream_ << MsgTypes::TransactionsPacketReply << bytes;

  cslog() << "NODE> Sending " << packet.transactionsCount() << " transaction(s)";

  flushCurrentTasks();
}

void Node::getBlockRequest(const uint8_t* data, const size_t size, const cs::PublicKey& sender) {
  if (myLevel_ != NodeLevel::Normal && myLevel_ != NodeLevel::Confidant) {
    return;
  }

  if (sender == myPublicKey_) {
    return;
  }

  uint32_t requested_seq;

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
  if (seq == lastStartSequence_) {
    solver_->tmpStorageProcessing();
    return;
  }

  // FIXME: result of merge cs_dev branch
  if (awaitingSyncroBlock && awaitingRecBlockCount < 1 && false) {
    cslog() << "SENDBLOCKREQUEST> New request won't be sent, we're awaiting block:  " << sendBlockRequestSequence;

    awaitingRecBlockCount++;
    return;
  }

  csdebug() << "SENDBLOCKREQUEST> Composing the request";

  size_t lws;
  size_t globalSequence = getBlockChain().getGlobalSequence();

  if (globalSequence == 0) {
    globalSequence = roundNum_;
  }

  lws = getBlockChain().getLastWrittenSequence();

  float syncStatus = (1.0f - (globalSequence - lws) / globalSequence) * 100.0f;
  csdebug() << "SENDBLOCKREQUEST> Syncro_Status = " << (int)syncStatus << "%";

  sendBlockRequestSequence = seq;
  awaitingSyncroBlock      = true;
  awaitingRecBlockCount    = 0;

  uint8_t requestTo = rand() % (int)(MIN_CONFIDANTS);

  ostream_.init(BaseFlags::Signed, solver_->roundTable().confidants[requestTo]);
  ostream_ << MsgTypes::BlockRequest << roundNum_ << seq;

  flushCurrentTasks();

  csdebug() << "SENDBLOCKREQUEST> Sending request for block: " << seq;
}

void Node::getBlockReply(const uint8_t* data, const size_t size) {
  cslog() << __func__;

  csdb::Pool pool;

  istream_.init(data, size);
  istream_ >> pool;

  cslog() << "GETBLOCKREPLY> Getting block " << pool.sequence();

  if (pool.sequence() == sendBlockRequestSequence) {
    cslog() << "GETBLOCKREPLY> Block Sequence is Ok";

    solver_->gotBlockReply(std::move(pool));
    awaitingSyncroBlock = false;
    solver_->rndStorageProcessing();
  } else if ((pool.sequence() > sendBlockRequestSequence) && (pool.sequence() < lastStartSequence_))
    solver_->gotFreeSyncroBlock(std::move(pool));
  else
    return;
  if (getBlockChain().getGlobalSequence() > getBlockChain().getLastWrittenSequence()) {
    sendBlockRequest(getBlockChain().getLastWrittenSequence() + 1);
  } else {
    syncro_started = false;
  }
}

void Node::sendBlockReply(const csdb::Pool& pool, const cs::PublicKey& sender) {
  ostream_.init(BaseFlags::Broadcast | BaseFlags::Fragmented | BaseFlags::Compressed);
  composeMessageWithBlock(pool, MsgTypes::RequestedBlock);
  flushCurrentTasks();
}

void Node::becomeWriter() {
  myLevel_ = NodeLevel::Writer;
}

void Node::onRoundStart(const cs::RoundTable& roundTable) {
  if ((!solver_->isPoolClosed()) && (!solver_->getBigBangStatus())) {
    solver_->sendTL();
  }

  cslog() << "======================================== ROUND " << roundNum_
          << " ========================================";
  cslog() << "Node PK = " << cs::Utils::byteStreamToHex(myPublicKey_.data(), myPublicKey_.size());

  if (roundTable.general == myPublicKey_) {
    myLevel_ = NodeLevel::Main;
  }
  else {
    bool    found   = false;
    uint8_t conf_no = 0;

    for (auto& conf : roundTable.confidants) {
      if (conf == myPublicKey_) {
        myLevel_     = NodeLevel::Confidant;
        myConfNumber = conf_no;
        found        = true;
        break;
      }

      conf_no++;
    }

    if (!found) {
      myLevel_ = NodeLevel::Normal;
    }
  }

  // Pretty printing...
  cslog() << "Round " << roundNum_ << " started. Mynode_type:=" << myLevel_ << " Confidants: ";

  int i = 0;
  for (auto& e : roundTable.confidants) {
    cslog() << i << ". " << cs::Utils::byteStreamToHex(e.data(), e.size());
    i++;
  }

#ifdef SYNCRO
  if ((roundNum_ > getBlockChain().getLastWrittenSequence() + 1) || (getBlockChain().getBlockRequestNeed())) {
    sendBlockRequest(getBlockChain().getLastWrittenSequence() + 1);
    syncro_started = true;
  }
  if (roundNum_ == getBlockChain().getLastWrittenSequence() + 1) {
    syncro_started      = false;
    awaitingSyncroBlock = false;
  }
#endif

  solver_->nextRound();
  transport_->processPostponed(roundNum_);
}

bool Node::getSyncroStarted() {
  return syncro_started;
}

uint8_t Node::getMyConfNumber() {
  return myConfNumber;
}

void Node::addToPackageTemporaryStorage(const csdb::Pool& pool) {
  m_packageTemporaryStorage.push_back(pool);
}

void Node::initNextRound(const cs::RoundTable& roundTable) {
  roundNum_ = roundTable.round;

  sendRoundTable(roundTable);

  cslog() << "NODE> RoundNumber :" << roundNum_;

  onRoundStart(roundTable);
}

Node::MessageActions Node::chooseMessageAction(const RoundNum rNum, const MsgTypes type) {
  if (type == MsgTypes::NewCharacteristic && rNum <= roundNum_) {
    return MessageActions::Process;
  }

  if (type == MsgTypes::TransactionPacket) {
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

  cslog() << "NODE> Number of confidants :" << static_cast<int>(confSize);

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
  auto max    = LZ4_compressBound(bSize);
  auto memPtr = allocator_.allocateNext(max);

  auto realSize = LZ4_compress_default((const char*)data, (char*)memPtr.get(), bSize, memPtr.size());

  allocator_.shrinkLast(realSize);
  ostream_ << type << roundNum_ << bSize;
  ostream_ << std::string(static_cast<char*>(memPtr.get()), memPtr.size());
}
