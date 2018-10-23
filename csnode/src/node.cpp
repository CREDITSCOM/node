#include <solver/solver.hpp>

#include <csnode/nodecore.h>
#include <csnode/node.hpp>

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
, packStreamAllocator_(1 << 26, 5)      //TODO: fix me
, allocator_(1 << 26, 3)                //TODO: fix me
, ostream_(&packStreamAllocator_, myPublicKey_) {
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
  std::copy(myPublicForSig.begin(), myPublicForSig.end(), publicKey.begin());

  cs::PrivateKey privateKey;
  std::copy(myPrivateForSig.begin(), myPrivateForSig.end(), privateKey.begin());

  solver_->setKeysPair(publicKey, privateKey);
  //solver_->addInitialBalance();
  solver_->runSpammer();

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

  for (size_t i = 0; i < 32; i++) {
    public_key[i] = myPublicForSig[i];
  }

  for (size_t i = 0; i < 64; i++) {
    private_key[i] = myPrivateForSig[i];
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

void Node::run() {
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

  transport_->clearTasks();
  onRoundStart(roundTable);

  // TODO: think how to improve this code
  cs::Timer::singleShot(TIME_TO_AWAIT_ACTIVITY * 10, [this, roundTable]() mutable {
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

  cslog() << "Hashes";

  const cs::Hashes& hashes = roundTable.hashes;

  for (std::size_t i = 0; i < hashes.size(); ++i) {
    cslog() << i << ". " << hashes[i].toString();
  }

  ostream_ << bytes;

  flushCurrentTasks();
}

void Node::sendAllRoundTransactionsPackets(const cs::RoundTable& roundTable)
{
  if (myLevel_ != NodeLevel::Writer) {
    return;
  }

  for (const auto& hash : roundTable.hashes) {
    const auto& hashTable = solver_->transactionsPacketTable();

    if (hashTable.count(hash)) {
      sendTransactionsPacket(hashTable.find(hash)->second);
    }
  }
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

  solver_->gotTransactionsPacket(std::move(packet));
}

void Node::getPacketHashesRequest(const uint8_t* data, const std::size_t size, const RoundNum round, const cs::PublicKey& sender) {
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

  solver_->gotPacketHashesRequest(std::move(hashes), round, sender);
}

void Node::getPacketHashesReply(const uint8_t* data, const std::size_t size) {
  cs::DataStream stream(data, size);

  cs::TransactionsPacket packet;
  stream >> packet;

  cslog() << "NODE> Get transactions hashes answer amount got " << packet.transactionsCount();
  cslog() << "NODE> Get transactions packet hash " << packet.hash().toString();

  if (packet.hash().isEmpty()) {
    cserror() << "NODE> Received transaction hashes answer packet hash is empty";
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

    hashes.push_back(hash);
  }

  roundTable.general = std::move(general);
  roundTable.confidants = std::move(confidants);
  roundTable.hashes = std::move(hashes);

  onRoundStart(roundTable);

  solver_->gotRound(std::move(roundTable));
}

void Node::getCharacteristic(const uint8_t* data, const size_t size, const cs::PublicKey& sender) {
  cslog() << "NODE> Characteric has arrived";

  if (!solver_->isPacketSyncFinished()) {
    cslog() << "NODE> Packet sync not finished, saving characteristic meta to call after sync";

    cs::Bytes characteristicBytes;
    characteristicBytes.assign(data, data + size);

    cs::CharacteristicMeta meta;
    meta.bytes = std::move(characteristicBytes);
    meta.round = solver_->roundTable().round;
    meta.sender = sender;

    solver_->addCharacteristicMeta(meta);
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

    solver_->addNotification(notification);
  }

  std::vector<cs::Hash> confidantsHashes;

  for (const auto& notification : solver_->notifications()) {
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

  std::optional<csdb::Pool> pool = solver_->applyCharacteristic(characteristic, poolMetaInfo);

  if (pool) {
    const uint8_t* message = pool->to_binary().data();
    const size_t messageSize = pool->to_binary().size();

    if (cs::Utils::verifySignature(signature, writerPublicKey, message, messageSize)) {
      cswarning() << "NODE> RECEIVED KEY Writer verification successfull";
      writeBlock(pool.value(), sequence, sender);
    }
    else {
      cswarning() << "NODE> RECEIVED KEY Writer verification failed";
    }
  }
}

void Node::writeBlock(csdb::Pool newPool, size_t sequence, const cs::PublicKey& sender) {
  csdebug() << "GOT NEW BLOCK: global sequence = " << sequence;

  if (sequence > this->getRoundNumber()) {
    return;  // remove this line when the block candidate signing of all trusted will be implemented
  }



  this->getBlockChain().setGlobalSequence(cs::numeric_cast<uint32_t>(sequence));

  if (sequence == (this->getBlockChain().getLastWrittenSequence() + 1)) {
    this->getBlockChain().putBlock(newPool);

#ifndef MONITOR_NODE
    if ((this->getMyLevel() != NodeLevel::Writer) && (this->getMyLevel() != NodeLevel::Main)) {
      auto hash = this->getBlockChain().getLastWrittenHash().to_string();

      this->sendHash(hash, sender);

      cslog() << "SENDING HASH to writer: " << hash;
    }
#endif
  }
}

void Node::getWriterNotification(const uint8_t* data, const std::size_t size, const cs::PublicKey& senderPublicKey) {
  if (!isCorrectNotification(data, size)) {
    cswarning() << "Notification failed " << cs::Utils::byteStreamToHex(senderPublicKey.data(), senderPublicKey.size());
    return;
  }

  cs::Bytes notification(data, data + size);
  solver_->addNotification(notification);

  if (!solver_->isEnoughNotifications(cs::Solver::NotificationState::Equal)) {
    return;
  }

  if (myLevel_ != NodeLevel::Writer) {
    return;
  }

  cslog() << "Confidants count more then 51%";
  
  applyNotifications();
}

void Node::applyNotifications() {
  csdebug() << "NODE> applyNotifications";

  cs::PoolMetaInfo poolMetaInfo;
  poolMetaInfo.sequenceNumber = 1 + bc_.getLastWrittenSequence();
  poolMetaInfo.timestamp = cs::Utils::currentTimestamp();

  const cs::Characteristic& characteristic = solver_->getCharacteristic();

  std::optional<csdb::Pool> pool = solver_->applyCharacteristic(characteristic, poolMetaInfo);

  if (!pool) {
    cserror() << "NODE> APPLY CHARACTERISTIC ERROR!";
    return;
  }

  const cs::Bytes& poolBinary = pool->to_binary();
  const cs::Signature poolSignature = cs::Utils::sign(poolBinary, solver_->getPrivateKey());
  cslog() << "NODE> applyNotification " << " Signature: " << cs::Utils::byteStreamToHex(poolSignature.data(), poolSignature.size());

  const bool isVerified = cs::Utils::verifySignature(poolSignature, solver_->getPublicKey(), poolBinary);
  cslog() << "NODE> after sign: isVerified == " << isVerified;

  writeBlock(pool.value(), poolMetaInfo.sequenceNumber, cs::PublicKey());

  ostream_.init(BaseFlags::Broadcast | BaseFlags::Compressed | BaseFlags::Fragmented);
  ostream_ << MsgTypes::NewCharacteristic << roundNum_;
  ostream_ << createBlockValidatingPacket(poolMetaInfo, characteristic, poolSignature, solver_->notifications());
  ostream_ << solver_->getPublicKey();

  flushCurrentTasks();
}

bool Node::isCorrectNotification(const uint8_t* data, const std::size_t size) {
  cs::DataStream stream(data, size);

  cs::Hash characteristicHash;
  stream >> characteristicHash;

  cs::Hash solverCharacteristicHash = solver_->getCharacteristicHash();

  if (characteristicHash != solverCharacteristicHash) {
    csdebug() << "NODE> Characteristic equals failed";
    csdebug() << "NODE> Received characteristic - " << cs::Utils::byteStreamToHex(characteristicHash.data(), characteristicHash.size());
    csdebug() << "NODE> Writer solver chracteristic - " << cs::Utils::byteStreamToHex(solverCharacteristicHash.data(), solverCharacteristicHash.size());
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

  size_t messageSize = size - signature.size() - publicKey.size();

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

void Node::sendPacketHashesRequest(const std::vector<cs::TransactionsPacketHash>& hashes) {
  if (myLevel_ == NodeLevel::Writer) {
    cserror() << "Writer should has all transactions hashes";
    return;
  }

  ostream_.init(BaseFlags::Fragmented | BaseFlags::Broadcast | BaseFlags::Compressed);

  cs::Bytes bytes;
  cs::DataStream stream(bytes);

  stream << hashes.size();

  for (const auto& hash : hashes) {
    stream << hash;
  }

  cslog() << "NODE> Sending transaction packet request: size: " << bytes.size();

  ostream_ << MsgTypes::TransactionsPacketRequest << roundNum_ << bytes;

  flushCurrentTasks();
}

void Node::sendPacketHashesReply(const cs::TransactionsPacket& packet, const cs::PublicKey& sender) {
  if (packet.hash().isEmpty()) {
    cswarning() << "Send transaction packet reply with empty hash failed";
    return;
  }

  ostream_.init(BaseFlags::Fragmented | BaseFlags::Compressed, sender);
  ostream_ << MsgTypes::TransactionsPacketReply << roundNum_;

  cs::Bytes bytes;
  cs::DataStream stream(bytes);

  stream << packet;

  cslog() << "Node> Sending transaction packet reply: size: " << bytes.size();

  ostream_ << bytes;

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

  float syncStatus = (1.0f - static_cast<float>(globalSequence - lws) / globalSequence) * 100.0f;
  csdebug() << "SENDBLOCKREQUEST> Syncro_Status = " << syncStatus << "%";

  sendBlockRequestSequence = seq;
  awaitingSyncroBlock      = true;
  awaitingRecBlockCount    = 0;

  uint8_t requestTo = rand() % (MIN_CONFIDANTS);

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
  }
  else if ((pool.sequence() > sendBlockRequestSequence) && (pool.sequence() < lastStartSequence_)) {
    solver_->gotFreeSyncroBlock(std::move(pool));
  }
  else {
    return;
  }
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
  cslog() << "NODE> Became writer";

  if (solver_->isEnoughNotifications(cs::Solver::NotificationState::GreaterEqual)) {
    applyNotifications();
  }
}

void Node::onRoundStart(const cs::RoundTable& roundTable) {
  if ((!solver_->isPoolClosed()) && (!solver_->getBigBangStatus())) {
    solver_->sendTL();
  }

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
      myConfidantIndex_ = std::distance(confidants.begin(), iter);
    }
    else {
      myLevel_ = NodeLevel::Normal;
    }
  }

  // Pretty printing...
  cslog() << "Round " << roundTable.round << " started. Mynode_type:=" << myLevel_ << " Confidants: ";

  for (std::size_t i = 0; i < confidants.size(); ++i) {
    const auto& confidant = confidants[i];
    cslog() << i << ". " << cs::Utils::byteStreamToHex(confidant.data(), confidant.size());
  }

  const cs::Hashes& hashes = roundTable.hashes;

  cslog() << "Transaction packets hashes count: " << hashes.size();

  for (std::size_t i = 0; i < hashes.size(); ++i) {
    cslog() << i << ". " << hashes[i].toString();
  }

#ifdef SYNCRO
  if ((roundNum_ > getBlockChain().getLastWrittenSequence() + 1) || (getBlockChain().getBlockRequestNeed())) {
    sendBlockRequest(getBlockChain().getLastWrittenSequence() + 1);
    syncro_started = true;
  }
  if (roundNum_ == getBlockChain().getLastWrittenSequence() + 1) {
    syncro_started = false;
    awaitingSyncroBlock = false;
  }
#endif

  solver_->nextRound();

#ifdef WRITER_RESEND_HASHES
  sendAllRoundTransactionsPackets(roundTable);
#endif

  transport_->processPostponed(roundNum_);
}

bool Node::getSyncroStarted() {
  return syncro_started;
}

uint8_t Node::getMyConfNumber() {
  return myConfidantIndex_;
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
  auto max    = LZ4_compressBound(cs::numeric_cast<int>(bSize));
  auto memPtr = allocator_.allocateNext(cs::numeric_cast<uint32_t>(max));

  auto realSize = LZ4_compress_default((const char*)data, (char*)memPtr.get(), bSize, memPtr.size());

  allocator_.shrinkLast(cs::numeric_cast<uint32_t>(realSize));
  ostream_ << type << roundNum_ << bSize;
  ostream_ << std::string(static_cast<char*>(memPtr.get()), memPtr.size());
}
