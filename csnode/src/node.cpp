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
const unsigned MAX_CONFIDANTS = 4;

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

  solver_->set_keys(myPublicForSig, myPrivateForSig);  // DECOMMENT WHEN SOLVER STRUCTURE WILL BE CHANGED!!!!
  solver_->addInitialBalance();

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
    } else
      return false;

  } else {
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
  uint8_t private_key[64], public_key[32];
  crypto_sign_ed25519_keypair(public_key, private_key);
  myPublicForSig.clear();
  myPrivateForSig.clear();
  std::string pub58, priv58;
  pub58  = EncodeBase58(myPublicForSig);
  priv58 = EncodeBase58(myPrivateForSig);

  myPublicForSig.resize(32);
  myPrivateForSig.resize(64);
  crypto_sign_ed25519_keypair(myPublicForSig.data(), myPrivateForSig.data());

  std::ofstream f_pub("NodePublic.txt");
  f_pub << EncodeBase58(myPublicForSig);
  f_pub.close();

  std::ofstream f_priv("NodePrivate.txt");
  f_priv << EncodeBase58(myPrivateForSig);
  f_priv.close();
}

bool Node::checkKeysForSig() {
  const uint8_t msg[] = {255, 0, 0, 0, 255};
  uint8_t       signature[64], public_key[32], private_key[64];
  for (int i = 0; i < 32; i++)
    public_key[i] = myPublicForSig[i];
  for (int i = 0; i < 64; i++)
    private_key[i] = myPrivateForSig[i];
  uint64_t sig_size;
  crypto_sign_ed25519_detached(signature, reinterpret_cast<unsigned long long*>(&sig_size), msg, 5, private_key);
  int ver_ok = crypto_sign_ed25519_verify_detached(signature, msg, 5, public_key);
  if (ver_ok == 0) {
    return true;
  } else {
    cslog() << "\n\nThe keys for node are not correct. Type \"g\" to generate or \"q\" to quit.";
    char gen_flag = 'a';
    std::cin >> gen_flag;
    if (gen_flag == 'g') {
      generateKeys();
      return true;
    } else
      return false;
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

void Node::getRoundTable(const uint8_t* data, const size_t size, const RoundNum rNum, uint8_t type) {
  istream_.init(data, size);

  cslog() << "NODE> Get Round Table";

  if (roundNum_ < rNum || type == MsgTypes::BigBang) {
    roundNum_ = rNum;
  } else {
    cswarning() << "Bad round number, ignoring";
    return;
  }

  if (!readRoundData(false)) {
    return;
  }

  if (myLevel_ == NodeLevel::Main) {
    if (!istream_.good()) {
      cswarning() << "Bad round table format, ignoring";
      return;
    }
  }

  if (myLevel_ == NodeLevel::Main) {
    if (!istream_.good()) {
      cswarning() << "Bad round table format, ignoring";
      return;
    }
  }

  cs::RoundInfo roundInfo;
  roundInfo.round      = rNum;
  roundInfo.confidants = confidantNodes_;
  roundInfo.hashes.clear();
  roundInfo.general = mainNode_;

  transport_->clearTasks();

  onRoundStart();

  solver_->gotRound(std::move(roundInfo));
}

void Node::getBigBang(const uint8_t* data, const size_t size, const RoundNum rNum, uint8_t type) {
  uint32_t lastBlock = getBlockChain().getLastWrittenSequence();
  if (rNum > lastBlock && rNum >= roundNum_) {
    getRoundTable(data, size, rNum, type);
    solver_->setBigBangStatus(true);
  } else {
    cslog() << "BigBang else";
  }
}

// the round table should be sent only to trusted nodes, all other should received only round number and Main node ID
void Node::sendRoundTable() {
  ostream_.init(BaseFlags::Broadcast);
  ostream_ << MsgTypes::RoundTable << roundNum_ << static_cast<uint8_t>(confidantNodes_.size()) << mainNode_;

  for (auto& conf : confidantNodes_) {
    ostream_ << conf;
  }

  cslog() << "------------------------------------------  SendRoundTable  ---------------------------------------";
  cslog() << "Round " << roundNum_ << ", General: " << cs::Utils::byteStreamToHex(mainNode_.str, 32) << "Confidants: ";

  int i = 0;

  for (auto& e : confidantNodes_) {
    if (e != mainNode_) {
      cslog() << i << ". " << cs::Utils::byteStreamToHex(e.str, 32);
      i++;
    }
  }

  transport_->clearTasks();

  flushCurrentTasks();
}

void Node::sendRoundTableUpdated(const cs::RoundInfo& round) {
  ostream_.init(BaseFlags::Broadcast);

  ostream_ << MsgTypes::Round << round.round << round.confidants.size() << round.hashes.size() << round.general;

  for (auto& it : round.confidants) {
    ostream_ << it;
  }

  for (auto& it : round.hashes) {
    ostream_ << it;
  }

  cslog() << "------------------------------------------  SendRoundTable  ---------------------------------------";
  cslog() << "Round " << roundNum_ << ", General: " << cs::Utils::byteStreamToHex(mainNode_.str, 32) << "Confidants: ";

  size_t i = 0;

  for (auto& e : confidantNodes_) {
    if (e != mainNode_) {
      cslog() << i << ". " << cs::Utils::byteStreamToHex(e.str, 32);
      ++i;
    }
  }

  i = 0;

  cslog() << "Hashes";

  for (auto& e : round.hashes) {
    cslog() << i << ". " << e.toString().c_str();
    i++;
  }

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

void Node::getRoundTableRequest(const uint8_t* data, const size_t size, const PublicKey& sender) {
  istream_.init(data, size);

  size_t rNum;
  istream_ >> rNum;

  if (rNum >= roundNum_) {
    return;
  }

  cslog() << "NODE> Get RT request from " << cs::Utils::byteStreamToHex(sender.str, 32);

  if (!istream_.good()) {
    cswarning() << "Bad RoundTableRequest format";
    return;
  }

  sendRoundTable();
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

  auto&    trx = pool.transactions();
  uint16_t i   = 0;

  for (auto& tr : trx) {
    cslog() << "NODE> Get transaction #:" << i << " from " << tr.source().to_string() << " ID= " << tr.innerID();

    solver_->gotTransaction(std::move(tr));
    i++;
  }
}

void Node::sendTransaction(csdb::Pool&& transactions) {
  transactions.recount();
  ostream_.init(BaseFlags::Fragmented | BaseFlags::Compressed | BaseFlags::Broadcast);
  composeMessageWithBlock(transactions, MsgTypes::Transactions);
  flushCurrentTasks();
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
    LOG_ERROR("Only main nodes can initialize the consensus procedure");
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

void Node::sendVectorRequest(const PublicKey& node) {
  if (myLevel_ != NodeLevel::Confidant) {
    cswarning() << "Only confidant nodes can send vectors";
    return;
  }

  cslog() << "NODE> Sending vector request to  " << cs::Utils::byteStreamToHex(node.str, 32);

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

void Node::sendWritingConfirmation(const PublicKey& node) {
  if (myLevel_ != NodeLevel::Confidant) {
    cserror() << "Only confidant nodes can send confirmation of the Writer";
    return;
  }

  cslog() << "NODE> Sending writing confirmation to  " << cs::Utils::byteStreamToHex(node.str, 32);

  ostream_.init(BaseFlags::Signed, node);
  ostream_ << MsgTypes::ConsVectorRequest << roundNum_ << getMyConfNumber();

  flushCurrentTasks();
}

void Node::getWritingConfirmation(const uint8_t* data, const size_t size, const PublicKey& sender) {
  if (myLevel_ != NodeLevel::Confidant) {
    return;
  }

  cslog() << "NODE> Getting WRITING CONFIRMATION from " << cs::Utils::byteStreamToHex(sender.str, 32);

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

  cslog() << "NODE> Sending TransactionList request to  " << cs::Utils::byteStreamToHex(mainNode_.str, 32);

  ostream_.init(BaseFlags::Signed, mainNode_);
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

  if (num < getConfidants().size()) {
    sendMatrix(solver_->getMyMatrix());
  }
}

void Node::sendMatrixRequest(const PublicKey& node) {
  if (myLevel_ != NodeLevel::Confidant) {
    cserror() << "Only confidant nodes can send vectors";
    return;
  }

  cslog() << "NODE> Sending vector request to  " << cs::Utils::byteStreamToHex(node.str, 32);

  ostream_.init(BaseFlags::Signed, node);
  ostream_ << MsgTypes::ConsMatrixRequest << roundNum_ << 1;
  flushCurrentTasks();
}

void Node::getMatrixRequest(const uint8_t* data, const size_t size) {
  if (myLevel_ != NodeLevel::Confidant) {
    return;
  }

  cslog() << "NODE> Getting matrix Request";  //<<  byteStreamToHex(sender.str, 32)

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

void Node::getVector(const uint8_t* data, const size_t size, const PublicKey& sender) {
  if (myLevel_ != NodeLevel::Confidant) {
    return;
  }

  if (myPublicKey_ == sender) {
    return;
  }

  cslog() << "NODE> Getting vector from " << cs::Utils::byteStreamToHex(sender.str, 32);

  istream_.init(data, size);

  cs::HashVector vec;
  istream_ >> vec;

  if (!istream_.good() || !istream_.end()) {
    LOG_WARN("Bad vector packet format");
    return;
  }

  cslog() << "Got vector";

  solver_->gotVector(std::move(vec));
}

void Node::sendVector(const cs::HashVector& vector) {
  cslog() << "NODE> 0 Sending vector ";

  if (myLevel_ != NodeLevel::Confidant) {
    cserror() << "Only confidant nodes can send vectors";
    return;
  }

  ostream_.init(BaseFlags::Broadcast);
  ostream_ << MsgTypes::ConsVector << roundNum_ << vector;

  flushCurrentTasks();
}

void Node::getMatrix(const uint8_t* data, const size_t size, const PublicKey& sender) {
  if (myLevel_ != NodeLevel::Confidant) {
    return;
  }

  if (myPublicKey_ == sender) {
    return;
  }

  istream_.init(data, size);

  cs::HashMatrix mat;
  istream_ >> mat;

  cslog() << "NODE> Getting matrix from " << cs::Utils::byteStreamToHex(sender.str, 32);

  if (!istream_.good() || !istream_.end()) {
    cswarning() << "Bad matrix packet format";
    return;
  }

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
  ostream_ << MsgTypes::ConsMatrix << roundNum_ << matrix;

  flushCurrentTasks();
}

uint32_t Node::getRoundNumber() {
  return roundNum_;
}

void Node::getBlock(const uint8_t* data, const size_t size, const PublicKey& sender) {
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
  if (roundNum_ == blockSeq)
    getBlockChain().setGlobalSequence(blockSeq);
  if (localSeq >= blockSeq)
    return;

  if (!blocksReceivingStarted_) {
    blocksReceivingStarted_ = true;
    lastStartSequence_      = pool.sequence();
    csdebug() << "GETBLOCK> Setting first got block: " << lastStartSequence_;
  }

  if (pool.sequence() == getBlockChain().getLastWrittenSequence() + 1)
    solver_->gotBlock(std::move(pool), sender);
  else
    solver_->gotIncorrectBlock(std::move(pool), sender);
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

void Node::getBadBlock(const uint8_t* data, const size_t size, const PublicKey& sender) {
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

void Node::getHash(const uint8_t* data, const size_t size, const PublicKey& sender) {
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

  std::vector<uint8_t> bytes;
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

void Node::getPacketHashesRequest(const uint8_t* data, const std::size_t size, const PublicKey& sender) {
  cslog() << "NODE> getPacketHashesReques ";

  istream_.init(data, size);

  uint32_t hashesCount = 0;
  istream_ >> hashesCount;

  std::vector<cs::TransactionsPacketHash> hashes;
  hashes.reserve(hashesCount);

  for (std::size_t i = 0; i < hashesCount; ++i) {
    cs::TransactionsPacketHash hash;
    istream_ >> hash;

    hashes.push_back(std::move(hash));
  }

  cslog() << "NODE> Hashes request got size: " << hashesCount;

  if (!istream_.good() || !istream_.end()) {
    cswarning() << "Bad packet request format";
    return;
  }

  if (hashesCount != hashes.size()) {
    cserror() << "Bad hashes created";
    return;
  }

  solver_->gotPacketHashesRequest(std::move(hashes), sender);
}

void Node::getPacketHashesReply(const uint8_t* data, const std::size_t size) {
  istream_.init(data, size);

  cs::TransactionsPacket packet;
  istream_ >> packet;

  cslog() << "NODE> Transactions hashes answer amount got " << packet.transactionsCount();

  if (!istream_.good() || !istream_.end()) {
    cswarning() << "Bad transactions hashes answer packet format";
    return;
  }

  if (packet.hash().isEmpty()) {
    cserror() << "Received transaction hashes answer packet hash is empty";
    return;
  }

  solver_->gotPacketHashesReply(std::move(packet));
}

void Node::getRoundTableUpdated(const uint8_t* data, const size_t size, const RoundNum round) {
  cslog() << "NODE> RoundTableUpdated";

  istream_.init(data, size);

  uint8_t confidantsCount = 0;
  istream_ >> confidantsCount;

  if (confidantsCount == 0) {
    cserror() << "Bad confidants count in round table";
    return;
  }

  uint16_t hashesCount;
  istream_ >> hashesCount;

  cs::RoundInfo roundInfo;
  roundInfo.round = round;

  PublicKey general;
  istream_ >> general;

  cs::ConfidantsKeys confidants;
  confidants.reserve(confidantsCount);

  for (std::size_t i = 0; i < confidantsCount; ++i) {
    PublicKey key;
    istream_ >> key;

    confidants.push_back(std::move(key));
  }

  cs::Hashes hashes;
  hashes.reserve(hashesCount);

  for (std::size_t i = 0; i < hashesCount; ++i) {
    cs::TransactionsPacketHash hash;
    istream_ >> hash;

    hashes.push_back(std::move(hash));
  }

  if (!istream_.end() || !istream_.good()) {
    cserror() << "Bad round table parsing";
    return;
  }

  roundInfo.general    = std::move(general);
  roundInfo.confidants = std::move(confidants);
  roundInfo.hashes     = std::move(hashes);

  onRoundStart();

  solver_->gotRound(std::move(roundInfo));
}

void Node::sendCharacteristic(const cs::PoolMetaInfo& poolMetaInfo, const uint32_t maskBitsCount,
                              const std::vector<uint8_t>& characteristic) {
  if (myLevel_ != NodeLevel::Writer) {
    cserror() << "Only writer nodes can send blocks";
    return;
  }

  cslog() << "Send Characteristic: seq = " << poolMetaInfo.sequenceNumber;

  ostream_.init(BaseFlags::Broadcast | BaseFlags::Fragmented);
  ostream_ << MsgTypes::NewCharacteristic << roundNum_;

  cs::DynamicBuffer buffer;
  cs::DataStream    stream(*buffer, buffer.size());

  std::string time     = poolMetaInfo.timestamp;
  uint64_t    sequence = poolMetaInfo.sequenceNumber;

  stream << static_cast<uint16_t>(time.size());
  stream << time;

  stream << maskBitsCount;

  stream << static_cast<uint32_t>(characteristic.size());
  stream << characteristic << sequence;

  ostream_ << std::string(stream.data(), stream.size());

  flushCurrentTasks();

  cslog() << "Send Characteristic: DONE size: " << stream.size();
}

void Node::getCharacteristic(const uint8_t* data, const size_t size, const PublicKey& sender) {
  cslog() << "Characteric has arrived";

  istream_.init(data, size);

  uint16_t    timeSize = 0;
  std::string time;

  uint32_t maskBitsCount;

  uint32_t             characteristicSize = 0;
  std::vector<uint8_t> characteristic;

  uint64_t sequence = 0;

  std::string allData;

  istream_ >> allData;

  cslog() << "Characteristic all data size: " << allData.size();

  cs::DataStream stream(const_cast<char*>(allData.data()), allData.size());

  stream >> timeSize;

  time.resize(timeSize);

  stream >> time >> maskBitsCount >> characteristicSize;

  characteristic.resize(characteristicSize);

  stream >> characteristic >> sequence;

  cs::PoolMetaInfo poolMetaInfo;
  poolMetaInfo.sequenceNumber = sequence;
  poolMetaInfo.timestamp      = time;

  std::vector<uint8_t> signature;
  signature.resize(cs::Utils::SignatureLength);

  stream >> signature;

  uint16_t notificationsSize = 0;
  stream >> notificationsSize;

  uint32_t bufferSize = 0;
  stream >> bufferSize;

  if (m_notifications.empty()) {
    cserror() << "Get characteristics, logical error";
  }

  cslog() << "Notifications size " << notificationsSize;
  cslog() << "Notification buffer size " << bufferSize;

  // TODO: write to blockchain pool
  for (std::size_t i = 0; i < notificationsSize; ++i) {
    std::vector<uint8_t> bytes;
    bytes.resize(bufferSize);

    stream >> bytes;

    m_notifications.push_back(cs::DynamicBufferPtr(new cs::DynamicBuffer(bytes.data(), bytes.size())));
  }

  std::vector<Hash> confidantsHashes;

  for (const auto& ptr : m_notifications) {
    Hash hash;

    std::copy(ptr->begin(), ptr->begin() + HASH_LENGTH, hash.str);
    confidantsHashes.push_back(hash);
  }

  Hash characteristicHash = getBlake2Hash(characteristic.data(), characteristic.size());

  for (const auto& hash : confidantsHashes) {
    if (hash != characteristicHash) {
      cserror() << "Some of confidants hashes is dirty";
      return;
    }
  }
  // TODO! how to Validate writer sugnature???

  cslog() << "GetCharacteristic " << poolMetaInfo.sequenceNumber << " maskbitCount" << maskBitsCount;
  cslog() << "Time >> " << poolMetaInfo.timestamp << "  << Time";

  solver_->applyCharacteristic(characteristic, maskBitsCount, poolMetaInfo, sender);
}

void Node::getNotification(const uint8_t* data, const std::size_t size, const PublicKey& senderPublicKey) {
  if (!isCorrectNotification(data, size, senderPublicKey)) {
    return;
  }

  m_notifications.push_back(cs::DynamicBufferPtr(new cs::DynamicBuffer(data, size)));

  if (m_notifications.size() < (getConfidants().size() / 2)) {
    return;
  }
  cslog() << "Confidants count more then 51%";

  cs::PoolMetaInfo poolMetaInfo;
  poolMetaInfo.sequenceNumber = 1 + bc_.getLastWrittenSequence();
  poolMetaInfo.timestamp      = cs::Utils::currentTimestamp();

  const cs::Characteristic& characteristic = solver_->getCharacteristic();
  solver_->applyCharacteristic(characteristic.mask, characteristic.size, poolMetaInfo, senderPublicKey);

  const std::vector<uint8_t>& poolBinary    = bc_.loadBlock(bc_.getLastHash()).to_binary();
  const std::vector<uint8_t>  poolSignature = cs::Utils::sign(poolBinary, solver_->getPrivateKey().data());

  ostream_.init(BaseFlags::Broadcast | BaseFlags::Compressed | BaseFlags::Fragmented);
  ostream_ << MsgTypes::NewCharacteristic << roundNum_;
  ostream_ << createBlockValidatingPacket(poolMetaInfo, characteristic, poolSignature, m_notifications);

  flushCurrentTasks();
}

bool Node::isCorrectNotification(const uint8_t* data, const std::size_t size, const PublicKey& senderPublicKey) {
  istream_.init(data, size);

  Hash characteristicHash;
  istream_ >> characteristicHash;
  PublicKey writerPublicKey;
  istream_ >> writerPublicKey;
  // TODO: need to extract signature too!

  const bool isCorrectWriterPublicKey     = writerPublicKey == myPublicKey_;
  const bool isCorrectChararcteristicHash = characteristicHash == solver_->getCharacteristicHash();

  const auto& confidants             = solver_->roundInfo().confidants;
  const auto  iterator               = std::find(confidants.begin(), confidants.end(), senderPublicKey);
  const bool  isFoundSenderPublicKey = iterator != std::end(confidants);

  return isCorrectWriterPublicKey && isCorrectChararcteristicHash && isFoundSenderPublicKey;
}

std::vector<uint8_t> Node::createBlockValidatingPacket(const cs::PoolMetaInfo&                  poolMetaInfo,
                                                      const cs::Characteristic&                characteristic,
                                                      const std::vector<uint8_t>&              signature,
                                                      const std::vector<cs::DynamicBufferPtr>& notifications) {
  cs::DynamicBuffer buffer;
  cs::DataStream    stream(*buffer, buffer.size());

  std::string time     = poolMetaInfo.timestamp;
  uint64_t    sequence = poolMetaInfo.sequenceNumber;

  stream << static_cast<uint16_t>(time.size());
  stream << time;

  stream << characteristic.size;

  stream << static_cast<uint32_t>(characteristic.mask.size());
  stream << characteristic.mask;
  stream << sequence;

  stream << signature;

  stream << static_cast<uint16_t>(notifications.size());

  if (!notifications.empty()) {
    stream << static_cast<uint32_t>(notifications.front()->size());
  }

  for (const auto& ptr : notifications) {
    stream << std::vector<uint8_t>(ptr->begin(), ptr->end());
  }

  return std::vector<uint8_t>(buffer.begin(), buffer.end());
}

void Node::sendNotification(const PublicKey& destination) {
  ostream_.init(BaseFlags::Direct | BaseFlags::Signed, destination);
  ostream_ << MsgTypes::WriterNotification;
  ostream_ << roundNum_;

  ostream_ << createNotification();

  flushCurrentTasks();
}

std::vector<uint8_t> Node::createNotification() {
  const Hash&      characteristicHash = solver_->getCharacteristicHash();
  const PublicKey& writerPublicKey    = solver_->getWriterPublicKey();

  std::vector<uint8_t> notification(HASH_LENGTH + PUBLIC_KEY_LENGTH);

  notification.insert(notification.end(), characteristicHash.str, characteristicHash.str + HASH_LENGTH);
  notification.insert(notification.end(), writerPublicKey.str, writerPublicKey.str + PUBLIC_KEY_LENGTH);

  std::vector<uint8_t> signedNotification = cs::Utils::sign(notification, solver_->getPrivateKey().data());
  return signedNotification;
}

void Node::sendHash(const std::string& hash, const PublicKey& target) {
  if (myLevel_ == NodeLevel::Writer || myLevel_ == NodeLevel::Main) {
    cserror() << "Writer and Main node shouldn't send hashes";
    return;
  }

  cswarning() << "Sending hash of " << roundNum_ << " to " << cs::Utils::byteStreamToHex(target.str, 32);
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

  std::size_t dataSize = hashes.size() * sizeof(cs::TransactionsPacketHash) + sizeof(uint32_t);

  cs::DynamicBuffer data(dataSize);
  cs::DataStream    stream(*data, data.size());

  stream << static_cast<uint32_t>(hashes.size());

  for (const auto& hash : hashes) {
    stream << hash;
  }

  cslog() << "Sending transaction packet request: size: " << dataSize;

  std::string compressed;
  snappy::Compress(static_cast<const char*>(*data), dataSize, &compressed);

  ostream_ << MsgTypes::TransactionsPacketRequest << compressed;

  flushCurrentTasks();
}

void Node::sendPacketHashesReply(const cs::TransactionsPacket& packet, const PublicKey& sender) {
  if (packet.hash().isEmpty()) {
    cslog() << "Send transaction packet reply with empty hash failed";
    return;
  }

  ostream_.init(BaseFlags::Fragmented | BaseFlags::Compressed, sender);

  auto     buffer   = packet.toBinary();
  void*    rawData  = buffer.data();
  uint32_t buffSize = buffer.size();

  cslog() << "Sending transaction packet reply: size: " << buffSize;

  std::string compressed;
  snappy::Compress(static_cast<const char*>(rawData), buffSize, &compressed);

  ostream_ << MsgTypes::TransactionsPacketReply << compressed;

  cslog() << "Sending transaction packet reply: compressed size: " << compressed.size();

  cslog() << "NODE> Sending " << packet.transactionsCount() << " transaction(s)";

  flushCurrentTasks();
}

void Node::getBlockRequest(const uint8_t* data, const size_t size, const PublicKey& sender) {
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
  ostream_.init(BaseFlags::Signed, confidantNodes_[requestTo]);
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

void Node::sendBlockReply(const csdb::Pool& pool, const PublicKey& sender) {
  ostream_.init(BaseFlags::Broadcast | BaseFlags::Fragmented | BaseFlags::Compressed);
  composeMessageWithBlock(pool, MsgTypes::RequestedBlock);
  flushCurrentTasks();
}

void Node::becomeWriter() {
  myLevel_ = NodeLevel::Writer;
}

void Node::onRoundStart() {
  if ((!solver_->isPoolClosed()) && (!solver_->getBigBangStatus())) {
    solver_->sendTL();
  }
  cslog() << "======================================== ROUND " << roundNum_
          << " ========================================";
  cslog() << "Node PK = " << cs::Utils::byteStreamToHex(myPublicKey_.str, 32);

  if (mainNode_ == myPublicKey_) {
    myLevel_ = NodeLevel::Main;
  } else {
    bool    found   = false;
    uint8_t conf_no = 0;

    for (auto& conf : confidantNodes_) {
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
  cslog() << "Round " << roundNum_ << " started. Mynode_type:=" << myLevel_ << "Confidants: ";

  int i = 0;
  for (auto& e : confidantNodes_) {
    cslog() << i << ". " << cs::Utils::byteStreamToHex(e.str, 32);
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

void Node::initNextRound(const cs::RoundInfo& roundInfo) {
  roundNum_ = roundInfo.round;
  mainNode_ = roundInfo.general;

  confidantNodes_.clear();

  for (auto& conf : roundInfo.confidants) {
    confidantNodes_.push_back(conf);
  }

  sendRoundTable();  // TODO: sendRoundTableUpdated(solver_->roundInfo());

  cslog() << "NODE> RoundNumber :" << roundNum_;

  onRoundStart();
}

Node::MessageActions Node::chooseMessageAction(const RoundNum rNum, const MsgTypes type) {
  if (type == MsgTypes::NewCharacteristic) {
    return MessageActions::Process;
  }
  if (type == MsgTypes::TransactionPacket) {
    return MessageActions::Process;
  }
  if (type == MsgTypes::BigBang && rNum > getBlockChain().getLastWrittenSequence()) {
    return MessageActions::Process;
  }
  if (type == MsgTypes::RoundTableRequest) {
    return (rNum < roundNum_ ? MessageActions::Process : MessageActions::Drop);
  }
  if (type == MsgTypes::RoundTable) {
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

inline bool Node::readRoundData(const bool tail) {
  PublicKey mainNode;
  uint8_t   confSize = 0;
  istream_ >> confSize;

  cslog() << "NODE> Number of confidants :" << static_cast<int>(confSize);

  if (confSize < MIN_CONFIDANTS || confSize > MAX_CONFIDANTS) {
    cswarning() << "Bad confidants num";
    return false;
  }

  std::vector<PublicKey> confidants;
  confidants.reserve(confSize);

  istream_ >> mainNode;

  while (istream_) {
    confidants.push_back(PublicKey());
    istream_ >> confidants.back();

    if (confidants.size() == confSize && !istream_.end()) {
      if (tail) {
        break;
      } else {
        cswarning() << "Too many confidant nodes received";
        return false;
      }
    }
  }

  if (!istream_.good() || confidants.size() < confSize) {
    cswarning() << "Bad round table format, ignoring";
    return false;
  }

  std::swap(confidants, confidantNodes_);

  cslog() << "NODE> RoundNumber :" << roundNum_;

  mainNode_ = mainNode;
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
