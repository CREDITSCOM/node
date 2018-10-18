#include <Solver/Solver.hpp>

#include <csnode/node.hpp>
#include <lib/system/logger.hpp>
#include <net/transport.hpp>

#include <base58.h>
#include <lz4.h>
#include <sodium.h>

#include <snappy.h>

const unsigned MIN_CONFIDANTS = 3;
const unsigned MAX_CONFIDANTS = 4;

Node::Node(const Config& config)
: myPublicKey_(config.getMyPublicKey())
, bc_(config.getPathToDB().c_str())
, solver_(new Credits::Solver(this))
,  // Credits::SolverFactory().createSolver(Credits::solver_type::fake, this)),
    transport_(new Transport(config, this))
, stats_(bc_)
, api_(bc_, solver_)
, allocator_(1 << 26, 3)
, packStreamAllocator_(1 << 26, 5)
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

  LOG_EVENT("Everything init");

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
                                          // std::cout <<
                                          // "//////////////////////////////////////////////////////////////////////////////////////////////////"
                                          // << std::endl;

  if (!pub.is_open() || !priv.is_open()) {
    std::cout << "\n\nNo suitable keys were found. Type \"g\" to generate or \"q\" to quit." << std::endl;
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
      std::cout << "\n\nThe size of keys found is not correct. Type \"g\" to generate or \"q\" to quit." << std::endl;
      char gen_flag = 'a';
      std::cin >> gen_flag;
      if (gen_flag == 'g') {
        generateKeys();
        return true;
      } else
        return false;
    }
    if (checkKeysForSig())
      return true;
    else
      return false;
  }
}

void Node::generateKeys() {
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

  memcpy(public_key, myPublicForSig.data(), 32);
  memcpy(private_key, myPrivateForSig.data(), 64);

  uint64_t sig_size;
  crypto_sign_ed25519_detached(signature, reinterpret_cast<unsigned long long*>(&sig_size), msg, 5, private_key);
  if (!crypto_sign_ed25519_verify_detached(signature, msg, 5, public_key)) {
    return true;
  } else {
    std::cout << "\n\nThe keys for node are not correct. Type \"g\" to generate or \"q\" to quit." << std::endl;
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

#ifdef MONITOR_NODE
bool monitorNode = true;
#else
bool monitorNode = false;
#endif

void Node::getRoundTable(const uint8_t* data, const size_t size, const RoundNum rNum, uint8_t type) {
  //std::cout << __func__ << std::endl;
  istream_.init(data, size);
#ifdef MYLOG
  std::cout << "NODE> Get Round Table" << std::endl;
#endif
  if (roundNum_ < rNum || type == MsgTypes::BigBang)
    roundNum_ = rNum;
  else {
    LOG_WARN("Bad round number, ignoring");
    return;
  }
  if (!readRoundData(false))
    return;

  if (myLevel_ == NodeLevel::Main)
    if (!istream_.good()) {
      LOG_WARN("Bad round table format, ignoring");
      return;
    }

  transport_->clearTasks();
  onRoundStart();

  // transport_->processPostponed(rNum);
}

void Node::getBigBang(const uint8_t* data, const size_t size, const RoundNum rNum, uint8_t type) {
  std::cout << __func__ << std::endl;

  istream_.init(data, size);
  Hash h;
  istream_ >> h;

  uint32_t lastBlock = getBlockChain().getLastWrittenSequence();
  getBlockChain().setGlobalSequence(lastBlock);
  solver_->clearAfterMax();

  if (lastBlock >= rNum) {
    getBlockChain().setLastWrittenSequence(rNum);
    getBlockChain().updateLastHash();
    lastBlock = rNum - 1;
  }

  if (lastBlock == (rNum - 1)) {
    Hash lHash((const char*)getBlockChain().getLastWrittenHash().to_binary().data());
    if (lHash != h) {
      LOG_WARN("Warning: deleting last block since the hashes don't match: " << byteStreamToHex(lHash.str, 32) << " vs " << byteStreamToHex(h.str, 32));
      getBlockChain().setLastWrittenSequence(lastBlock - 1);
      getBlockChain().updateLastHash();
    }
  }

  solver_->setBigBangStatus(true);
  getRoundTable(istream_.getCurrPtr(), size - HASH_LENGTH, rNum, type);
}

// the round table should be sent only to trusted nodes, all other should received only round number and Main node ID
void Node::sendRoundTable() {
  ostream_.init(BaseFlags::Broadcast);
  ostream_ << MsgTypes::RoundTable << roundNum_ << static_cast<uint8_t>(confidantNodes_.size()) << mainNode_;

  for (auto& conf : confidantNodes_)
    ostream_ << conf;

  // LOG_EVENT("Sending round table");
  std::cout << "------------------------------------------  SendRoundTable  ---------------------------------------"
            << std::endl;
  std::cout << "Round " << roundNum_ << ", General: " << byteStreamToHex(mainNode_.str, 32) << std::endl
            << "Confidants: " << std::endl;
  size_t i = 0;
  for (auto& e : confidantNodes_) {
    if (e != mainNode_) {
      std::cout << i << ". " << byteStreamToHex(e.str, 32) << std::endl;
      ++i;
    }
  }
  transport_->clearTasks();
  flushCurrentTasks();
}

void Node::sendRoundTableRequest(size_t rNum) {
  if (rNum < roundNum_)
    return;
#ifdef MYLOG
  std::cout << "rNum = " << rNum << ", real RoundNumber = " << roundNum_ << std::endl;
#endif
  ostream_.init(BaseFlags::Broadcast);
  ostream_ << MsgTypes::RoundTableRequest << roundNum_;
#ifdef MYLOG
  std::cout << "Sending RoundTable request" << std::endl;
#endif
  LOG_EVENT("Sending RoundTable request");
  flushCurrentTasks();
}

void Node::getRoundTableRequest(const uint8_t* data, const size_t size, const PublicKey& sender) {
  std::cout << __func__ << std::endl;
  istream_.init(data, size);
  size_t rNum;
  istream_ >> rNum;
  if (rNum >= roundNum_)
    return;
#ifdef MYLOG
  std::cout << "NODE> Get RT request from " << byteStreamToHex(sender.str, 32) << std::endl;
#endif
  if (!istream_.good()) {
    LOG_WARN("Bad RoundTableRequest format");
    return;
  }
  sendRoundTable();
}

void Node::getTransaction(const uint8_t* data, const size_t size) {
  istream_.init(data, size);

  csdb::Pool pool;
  istream_ >> pool;

  if (!istream_.good() || !istream_.end()) {
    LOG_WARN("Bad transaction packet format");
    return;
  }

  for (auto& t : pool.transactions()) {
    if (myLevel_ == NodeLevel::Main || myLevel_ == NodeLevel::Writer) {
      solver_->gotTransaction(std::move(t));
    }
  }
}

void Node::sendTransaction(csdb::Pool&& transactions) {
  transactions.recount();
  ostream_.init(BaseFlags::Fragmented | BaseFlags::Compressed | BaseFlags::Broadcast);
  composeMessageWithBlock(transactions, MsgTypes::Transactions);
  flushCurrentTasks();
}

void Node::getFirstTransaction(const uint8_t* data, const size_t size) {
  std::cout << __func__ << std::endl;
  if (myLevel_ != NodeLevel::Confidant) {
    return;
  }

  istream_.init(data, size);

  csdb::Transaction trans;
  istream_ >> trans;

  if (!istream_.good() || !istream_.end()) {
    LOG_WARN("Bad transaction packet format");
    return;
  }
  csdb::Pool pool_;
  pool_.add_transaction(trans);

  LOG_EVENT("Got first transaction, initializing consensus...");
  solver_->gotTransactionList(std::move(pool_));
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
  //std::cout << __func__ << std::endl;
  if (myLevel_ != NodeLevel::Confidant) {
    return;
  }
  csdb::Pool pool;
  pool = csdb::Pool{};
#ifdef MYLOG
  std::cout << "Getting List: list size: " << size << std::endl;
#endif
  // std::cout << "Getting List: " << byteStreamToHex((const char*)data,size) << std::endl;
  if (!((size == 0) || (size > 2000000000))) {
    //  std::cout << "NODE> Get transaction list 1 " << std::endl;
    istream_.init(data, size);
    // std::cout << "NODE> Get transaction list 2 " << std::endl;

    // std::cout << "NODE> Get transaction list 3 tNum = " << std::endl;
    istream_ >> pool;
    if (!istream_.good() || !istream_.end()) {
      LOG_WARN("Bad transactions list packet format");
      pool = csdb::Pool{};
    }
#ifdef MYLOG
    std::cout << "NODE> Transactions amount got " << pool.transactions_count() << std::endl;
#endif

    LOG_EVENT("Got full transactions list of " << pool.transactions_count());
    // if (pool.transactions_count()>0)
  }
  solver_->gotTransactionList(std::move(pool));
  // }
}

void Node::sendTransactionList(const csdb::Pool& pool) {  //, const PublicKey& target) {
  if ((myLevel_ == NodeLevel::Confidant) || (myLevel_ == NodeLevel::Normal)) {
    LOG_ERROR("Only main nodes can send transaction lists");
    return;
  }

  ostream_.init(BaseFlags::Fragmented | BaseFlags::Compressed | BaseFlags::Broadcast);
  composeMessageWithBlock(pool, MsgTypes::TransactionList);

  // const char* bl = pool.to_byte_stream(size);
  // void* tmp = ostream_.getPackets()->data();
  // std::cout << "Data from TList: " << byteStreamToHex((const char*)data, bSize) << std::endl;
#ifdef MYLOG
  std::cout << "NODE> Sending " << pool.transactions_count() << " transaction(s)" << std::endl;
#endif
  flushCurrentTasks();
}

// void Node::sendTLConfirmation(size_t tcount)
//{
//  if ((myLevel_ == NodeLevel::Confidant) || (myLevel_ == NodeLevel::Normal)) {
//    LOG_ERROR("Only previous main nodes can send Transaction Lists ");
//    return;
//  }
//
//  ostream_.init(BaseFlags::Signed, mainNode_);
//  ostream_ << MsgTypes::TLConfirmation
//    << roundNum_
//    << tcount;
//  std::cout << "NODE> Transactions amount sent " << tcount << " to " << byteStreamToHex(mainNode_.str, 32) <<
//  std::endl; flushCurrentTasks();
//}
//
// void Node::getTLConfirmation(const uint8_t* data, const size_t size)
//{
//  std::cout << __func__ << std::endl;
//  std::cout << "NODE> Transactions amount got " << std::endl;
//  if (myLevel_ != NodeLevel::Main) {
//    LOG_ERROR("Only main nodes can receive TLSend confirmations");
//    return;
//  }
//  istream_.init(data, size);
//
//  size_t trNum;
//  istream_ >> trNum;
//  std::cout << "NODE> Transactions amount got " << trNum << std::endl;
//  solver_->setLastRoundTransactionsGot(trNum);
//}

void Node::sendVectorRequest(const PublicKey& node) {
  if (myLevel_ != NodeLevel::Confidant) {
    LOG_ERROR("Only confidant nodes can send vectors");
    return;
  }
#ifdef MYLOG
  std::cout << "NODE> Sending vector request to  " << byteStreamToHex(node.str, 32) << std::endl;
#endif
  ostream_.init(BaseFlags::Signed, node);
  ostream_ << MsgTypes::ConsVectorRequest << roundNum_ << 1;
  flushCurrentTasks();
}

void Node::getVectorRequest(const uint8_t* data, const size_t size)  //, const PublicKey& sender) {
{
  std::cout << __func__ << std::endl;
  // std::cout << "NODE> Getting vector" << std::endl;
  if (myLevel_ != NodeLevel::Confidant) {
    return;
  }
  // if (myPublicKey_ == sender) return;
#ifdef MYLOG
  std::cout << "NODE> Getting vector Request from " << std::endl;  // byteStreamToHex(sender.str, 32) <<
#endif
  istream_.init(data, size);

  int num;
  istream_ >> num;
  if (num == 1)
    sendVector(solver_->getMyVector());

  if (!istream_.good() || !istream_.end()) {
    LOG_WARN("Bad vector packet format");
    return;
  }
}

void Node::sendWritingConfirmation(const PublicKey& node) {
  if (myLevel_ != NodeLevel::Confidant) {
    LOG_ERROR("Only confidant nodes can send confirmation of the Writer");
    return;
  }
#ifdef MYLOG
  std::cout << "NODE> Sending writing confirmation to  " << byteStreamToHex(node.str, 32) << std::endl;
#endif
  ostream_.init(BaseFlags::Signed, node);
  ostream_ << MsgTypes::ConsVectorRequest << roundNum_ << getMyConfNumber();
  flushCurrentTasks();
}

void Node::getWritingConfirmation(const uint8_t* data, const size_t size, const PublicKey& sender) {
  // std::cout << __func__ << std::endl;
  if (myLevel_ != NodeLevel::Confidant) {
    return;
  }
  // if (myPublicKey_ == sender) return;
#ifdef MYLOG
  std::cout << "NODE> Getting WRITING CONFIRMATION from " << byteStreamToHex(sender.str, 32) << std::endl;
#endif
  istream_.init(data, size);

  uint8_t confNumber_;
  istream_ >> confNumber_;
  if (!istream_.good() || !istream_.end()) {
    LOG_WARN("Bad vector packet format");
    return;
  }
  if (confNumber_ < 3)
    solver_->addConfirmation(confNumber_);
}

void Node::sendTLRequest() {
  if ((myLevel_ != NodeLevel::Confidant) || (roundNum_ < 2)) {
    LOG_ERROR("Only confidant nodes need TransactionList");
    return;
  }
#ifdef MYLOG
  std::cout << "NODE> Sending TransactionList request to  " << byteStreamToHex(mainNode_.str, 32) << std::endl;
#endif
  ostream_.init(BaseFlags::Signed, mainNode_);
  ostream_ << MsgTypes::ConsTLRequest << getMyConfNumber();
  flushCurrentTasks();
}

void Node::getTlRequest(const uint8_t* data, const size_t size, const PublicKey& sender) {
  std::cout << __func__ << std::endl;
  // std::cout << "NODE> Getting vector" << std::endl;
  if (myLevel_ != NodeLevel::Main) {
    LOG_ERROR("Only main nodes can send TransactionList");
    return;
  }
  // if (myPublicKey_ == sender) return;
#ifdef MYLOG
  std::cout << "NODE> Getting TransactionList request" << std::endl;  // byteStreamToHex(sender.str, 32) <<
#endif
  istream_.init(data, size);

  uint8_t num;
  istream_ >> num;

  if (!istream_.good() || !istream_.end()) {
    // LOG_WARN("Bad vector packet format");
    return;
  }
  if (num < getConfidants().size())
    sendMatrix(solver_->getMyMatrix());
}

void Node::sendMatrixRequest(const PublicKey& node) {
  if (myLevel_ != NodeLevel::Confidant) {
    //  LOG_ERROR("Only confidant nodes can send vectors");
    return;
  }
#ifdef MYLOG
  std::cout << "NODE> Sending vector request to  " << byteStreamToHex(node.str, 32) << std::endl;
#endif
  ostream_.init(BaseFlags::Signed, node);
  ostream_ << MsgTypes::ConsMatrixRequest << roundNum_ << 1;
  flushCurrentTasks();
}

void Node::getMatrixRequest(const uint8_t* data, const size_t size)  //, const PublicKey& sender) {
{
  std::cout << __func__ << std::endl;
  // std::cout << "NODE> Getting vector" << std::endl;
  if (myLevel_ != NodeLevel::Confidant) {
    return;
  }
  // if (myPublicKey_ == sender) return;
#ifdef MYLOG
  std::cout << "NODE> Getting matrix Request" << std::endl;  //<<  byteStreamToHex(sender.str, 32)
#endif
  istream_.init(data, size);

  int num;
  istream_ >> num;
  if (!istream_.good() || !istream_.end()) {
    LOG_WARN("Bad vector packet format");
    return;
  }
  if (num == 1)
    sendMatrix(solver_->getMyMatrix());
}

void Node::getVector(const uint8_t* data, const size_t size, const PublicKey& sender) {
  std::cout << __func__ << std::endl;
  // std::cout << "NODE> Getting vector" << std::endl;
  if (myLevel_ != NodeLevel::Confidant) {
    return;
  }
  if (myPublicKey_ == sender)
    return;
#ifdef MYLOG
  std::cout << "NODE> Getting vector from " << byteStreamToHex(sender.str, 32) << std::endl;
#endif
  istream_.init(data, size);

  Credits::HashVector vec;
  istream_ >> vec;

  if (!istream_.good() || !istream_.end()) {
    LOG_WARN("Bad vector packet format");
    return;
  }

  //LOG_EVENT("Got vector");
  solver_->gotVector(std::move(vec));
#ifdef MYLOG
  std::cout << "NODE>  WE returned!!!" << std::endl;
#endif
}

void Node::sendVector(const Credits::HashVector& vector) {
#ifdef MYLOG
  std::cout << "NODE> 0 Sending vector " << std::endl;
#endif
  if (myLevel_ != NodeLevel::Confidant) {
    LOG_ERROR("Only confidant nodes can send vectors");
    return;
  }
  // std::cout << "NODE> Sending vector " << std::endl;
  // for (auto& it : confidantNodes_)
  // {
  // if(it==myPublicKey_) continue;
  // std::cout << "NODE> 1 Sending vector to " << std::endl;//<< byteStreamToHex(it.str, 32)
  ostream_.init(BaseFlags::Broadcast);  //, it);
  ostream_ << MsgTypes::ConsVector << roundNum_ << vector;

  flushCurrentTasks();
  // }
}

void Node::getMatrix(const uint8_t* data, const size_t size, const PublicKey& sender) {
  std::cout << __func__ << std::endl;
  if (myLevel_ != NodeLevel::Confidant) {
    return;
  }
  if (myPublicKey_ == sender)
    return;
  istream_.init(data, size);

  Credits::HashMatrix mat;
  istream_ >> mat;
#ifdef MYLOG
  std::cout << "NODE> Getting matrix from " << byteStreamToHex(sender.str, 32) << std::endl;
#endif
  if (!istream_.good() || !istream_.end()) {
    LOG_WARN("Bad matrix packet format");
    return;
  }

  //LOG_EVENT("Got matrix");
  solver_->gotMatrix(std::move(mat));
}

void Node::sendMatrix(const Credits::HashMatrix& matrix) {
#ifdef MYLOG
  std::cout << "NODE> 0 Sending matrix to " << std::endl;
#endif
  if (myLevel_ != NodeLevel::Confidant) {
    LOG_ERROR("Only confidant nodes can send matrices");
    return;
  }

  // for (auto& it : confidantNodes_)
  // {
  //  if (it == myPublicKey_) continue;
#ifdef MYLOG
  std::cout << "NODE> 1 Sending matrix to " << std::endl;  //<< byteStreamToHex(it.str, 32)
#endif
  ostream_.init(BaseFlags::Broadcast | BaseFlags::Fragmented);  //, it);
  ostream_ << MsgTypes::ConsMatrix << roundNum_ << matrix;

  flushCurrentTasks();
  // }
}

uint32_t Node::getRoundNumber() {
  return roundNum_;
}

void Node::getBlock(const uint8_t* data, const size_t size, const PublicKey& sender) {
  std::cout << __func__ << std::endl;
  if (myLevel_ == NodeLevel::Writer) {
    LOG_WARN("Writer cannot get blocks");
    return;
  }

  // myLevel_ = NodeLevel::Normal; //test feature
  istream_.init(data, size);

  csdb::Pool pool;
  istream_ >> pool;

  if (!istream_.good() || !istream_.end()) {
    LOG_WARN("Bad block packet format");
    return;
  }

  LOG_DEBUG("Got block of " << pool.transactions_count() << " transactions of seq " << pool.sequence() << " and hash "
                            << pool.hash().to_string() << " and ts " << pool.user_field(0).value<std::string>());

  // LOG_EVENT("Got block of " << pool.transactions_count() <<" transactions");

  size_t localSeq = getBlockChain().getLastWrittenSequence();
  size_t blockSeq = pool.sequence();
  if (roundNum_ == blockSeq) getBlockChain().setGlobalSequence(blockSeq);
  if (localSeq >= blockSeq) return;

  if (!blocksReceivingStarted_)
  {
    blocksReceivingStarted_ = true;
    lastStartSequence_ = pool.sequence();
    std::cout << "GETBLOCK> Setting first got block: " << lastStartSequence_ << std::endl;
  }

  //LOG_EVENT("Got block of " << pool.transactions_count() << " transactions");

  solver_->rndStorageProcessing();

  if (pool.sequence() == getBlockChain().getLastWrittenSequence() + 1) {
    if (monitorNode || getBlockChain().getLastHash() == pool.previous_hash())
      solver_->gotBlock(std::move(pool), sender);
    else {
      auto nSeq = pool.sequence() > 2 ? (pool.sequence() - 2) : 1;
      getBlockChain().setLastWrittenSequence(nSeq);
      getBlockChain().updateLastHash();
      solver_->gotIncorrectBlock(std::move(pool), sender);
      sendBlockRequest(nSeq);
    }
  }
  else solver_->gotIncorrectBlock(std::move(pool), sender);
}

void Node::sendBlock(const csdb::Pool& pool) {
  if (myLevel_ != NodeLevel::Writer) {
    LOG_ERROR("Only writer nodes can send blocks");
    return;
  }

  ostream_.init(BaseFlags::Broadcast | BaseFlags::Fragmented | BaseFlags::Compressed);
  composeMessageWithBlock(pool, MsgTypes::NewBlock);

  LOG_DEBUG("Sending block of " << pool.transactions_count() << " transactions of seq " << pool.sequence()
                                << " and hash " << pool.hash().to_string() << " and ts "
                                << pool.user_field(0).value<std::string>());
  flushCurrentTasks();
}

void Node::getBadBlock(const uint8_t* data, const size_t size, const PublicKey& sender) {
  //std::cout << __func__ << std::endl;
  if (myLevel_ == NodeLevel::Writer) {
    LOG_WARN("Writer cannot get bad blocks");
    return;
  }

  // myLevel_ = NodeLevel::Normal; //test feature
  istream_.init(data, size);

  csdb::Pool pool;
  istream_ >> pool;

  if (!istream_.good() || !istream_.end()) {
    LOG_WARN("Bad block packet format");
    return;
  }

  LOG_EVENT("Got block of " << pool.transactions_count() << " transactions");
  solver_->gotBadBlockHandler(std::move(pool), sender);
}

void Node::sendBadBlock(const csdb::Pool& pool) {
  if (myLevel_ != NodeLevel::Writer) {
    LOG_ERROR("Only writer nodes can send bad blocks");
    return;
  }

  ostream_.init(BaseFlags::Broadcast | BaseFlags::Fragmented | BaseFlags::Compressed);
  composeMessageWithBlock(pool, MsgTypes::NewBadBlock);

  LOG_EVENT("Sending bad block of " << pool.transactions_count() << " transactions");
  // flushCurrentTasks();
}

void Node::getHash(const uint8_t* data, const size_t size, const PublicKey& sender) {
  std::cout << __func__ << std::endl;
  if (myLevel_ != NodeLevel::Writer) {
    std::cout << "Non-Writers cannot get hashes" << std::endl;
    return;
  }
  LOG_DEBUG("Getting hash from " << byteStreamToHex(sender.str, 32));
  istream_.init(data, size);

  Hash hash;
  istream_ >> hash;

  if (!istream_.good() || !istream_.end()) {
    LOG_DEBUG("Bad hash packet format");
    return;
  }

  solver_->gotHash(hash, sender);
}

void Node::sendHash(const Hash& hash, const PublicKey& target) {
  if (myLevel_ == NodeLevel::Writer || myLevel_ == NodeLevel::Main) {
    LOG_ERROR("Writer and Main node shouldn't send hashes");
    return;
  }

  LOG_WARN("Sending hash of " << roundNum_ << " to " << byteStreamToHex(target.str, 32));

  ostream_.init(BaseFlags::Signed | BaseFlags::Encrypted, target);
  ostream_ << MsgTypes::BlockHash << roundNum_ << hash;
  flushCurrentTasks();
}

void Node::getBlockRequest(const uint8_t* data, const size_t size, const PublicKey& sender) {
  //std::cout << __func__ << std::endl;
  //if (myLevel_ != NodeLevel::Normal && myLevel_ != NodeLevel::Confidant)
  //  return;
  //if (sender == myPublicKey_)
  //  return;
  uint32_t requested_seq;
  istream_.init(data, size);
  istream_ >> requested_seq;
#ifdef MYLOG
  std::cout << "GETBLOCKREQUEST> Getting the request for block: " << requested_seq << std::endl;
#endif
  if (requested_seq > getBlockChain().getLastWrittenSequence()) {
#ifdef MYLOG
    std::cout << "GETBLOCKREQUEST> The requested block: " << requested_seq << " is BEYOND my CHAIN" << std::endl;
#endif
    return;
  }

  solver_->gotBlockRequest(std::move(getBlockChain().getHashBySequence(requested_seq)), sender);
}

void Node::sendBlockRequest(uint32_t seq) {
  static uint32_t lfReq, lfTimes;

  solver_->rndStorageProcessing();
  seq = getBlockChain().getLastWrittenSequence() + 1;

  size_t lws, gs;

  if (getBlockChain().getGlobalSequence() == 0)
    gs = roundNum_;
  else
    gs = getBlockChain().getGlobalSequence();

  lws = getBlockChain().getLastWrittenSequence();

  const float syncStatus = (1. - (gs * 1. - lws * 1.) / gs) * 100.;
  if ((int)syncStatus <= 100) {
    std::cout << "SYNC: [";
    for (uint32_t i = 0; i < (int)syncStatus; ++i) if (i % 2) std::cout << "#";
    for (uint32_t i = (int)syncStatus; i < 100; ++i) if (i % 2) std::cout << "-";
    std::cout << "] " << (int)syncStatus << "%" << std::endl;
  }

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
      LOG_WARN("Sending request for block " << reqSeq << " from nbr " << requestee->in);
      ostream_.init(BaseFlags::Direct | BaseFlags::Signed);
      ostream_ << MsgTypes::BlockRequest << roundNum_ << reqSeq;
      transport_->deliverDirect(ostream_.getPackets(), ostream_.getPacketsCount(), requestee);
      if (lfReq == reqSeq && ++lfTimes >= 4)
        transport_->sendBroadcast(ostream_.getPackets());
      ostream_.clear();
    }

    reqSeq = solver_->getNextMissingBlock(reqSeq);
  }

  //#endif
  sendBlockRequestSequence = seq;
  awaitingSyncroBlock      = true;
  awaitingRecBlockCount    = 0;

#ifdef MYLOG
  //std::cout << "SENDBLOCKREQUEST> Sending request for block: " << seq << std::endl;
#endif
}

void Node::getBlockReply(const uint8_t* data, const size_t size) {
  std::cout << __func__ << std::endl;
  csdb::Pool pool;

  istream_.init(data, size);
  istream_ >> pool;
#ifdef MYLOG
  std::cout << "GETBLOCKREPLY> Getting block " << pool.sequence() << std::endl;
#endif

  transport_->syncReplied(pool.sequence());

  if (getBlockChain().getGlobalSequence() < pool.sequence())
    getBlockChain().setGlobalSequence(pool.sequence());

  if (pool.sequence() == (getBlockChain().getLastWrittenSequence() + 1)) {
#ifdef MYLOG
    std::cout << "GETBLOCKREPLY> Block Sequence is Ok" << std::endl;
#endif

    if (monitorNode || getBlockChain().getLastHash() == pool.previous_hash()) {
      solver_->gotBlockReply(std::move(pool));
      awaitingSyncroBlock = false;
      solver_->rndStorageProcessing();
    }
    else {
      auto nSeq = pool.sequence() > 2 ? (pool.sequence() - 2) : 1;
      getBlockChain().setLastWrittenSequence(nSeq);
      getBlockChain().updateLastHash();
      solver_->gotFreeSyncroBlock(std::move(pool));
    }
  }
  else if (pool.sequence() > getBlockChain().getLastWrittenSequence())
    solver_->gotFreeSyncroBlock(std::move(pool));

  if (getBlockChain().getGlobalSequence() >
      getBlockChain().getLastWrittenSequence())  //&&(getBlockChain().getGlobalSequence()<=roundNum_))
    sendBlockRequest(getBlockChain().getLastWrittenSequence() + 1);
  else {
    syncro_started = false;
#ifdef MYLOG
    std::cout << "SYNCRO FINISHED!!!" << std::endl;
#endif
  }
}

void Node::sendBlockReply(const csdb::Pool& pool, const PublicKey& sender) {
#ifdef MYLOG
  std::cout << "SENDBLOCKREPLY> Sending block " << pool.sequence() << " of " << const_cast<csdb::Pool&>(pool).transactions().size() << " trxns to " << byteStreamToHex(sender.str, 32) << std::endl;
#endif

  ConnectionPtr conn = transport_->getConnectionByKey(sender);
  if (!conn) {
    LOG_WARN("Cannot get a connection with a specified public key");
    return;
  }

  ostream_.init(BaseFlags::Direct | BaseFlags::Broadcast | BaseFlags::Fragmented | BaseFlags::Compressed);
  composeMessageWithBlock(pool, MsgTypes::RequestedBlock);
  transport_->deliverDirect(ostream_.getPackets(),
                            ostream_.getPacketsCount(),
                            conn);
  ostream_.clear();
}

void Node::becomeWriter() {
  // if (myLevel_ != NodeLevel::Main && myLevel_ != NodeLevel::Confidant)
  //  LOG_WARN("Logically impossible to become a writer right now");

  myLevel_ = NodeLevel::Writer;
}

void Node::onRoundStart() {
  if ((!solver_->mPoolClosed()) && (!solver_->getBigBangStatus())) {
    solver_->sendTL();
  }
  std::cout << "======================================== ROUND " << roundNum_
            << " ========================================" << std::endl;
  std::cout << "Node PK = " << byteStreamToHex(myPublicKey_.str, 32) << std::endl;

  if (mainNode_ == myPublicKey_)
    myLevel_ = NodeLevel::Main;
  else {
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

    if (!found)
      myLevel_ = NodeLevel::Normal;
  }

  // Pretty printing...
  std::cout << "Round " << roundNum_ << " started. Mynode_type:=" << myLevel_
            << ", General: " << byteStreamToHex(mainNode_.str, 32) << std::endl
            << "Confidants: " << std::endl;
  int i = 0;
  for (auto& e : confidantNodes_) {
    std::cout << i << ". " << byteStreamToHex(e.str, 32) << std::endl;
    i++;
  }

  solver_->nextRound();
  transport_->processPostponed(roundNum_);

#ifdef SYNCRO
  if ((roundNum_ > getBlockChain().getLastWrittenSequence() + 1) ||
      (getBlockChain().getBlockRequestNeed()))
  {
    sendBlockRequest(getBlockChain().getLastWrittenSequence() + 1);
    syncro_started = true;
  }
  if (roundNum_ == getBlockChain().getLastWrittenSequence() + 1) {
    syncro_started      = false;
    awaitingSyncroBlock = false;
  }
#endif  // !1
}

bool Node::getSyncroStarted() {
  return syncro_started;
}

uint8_t Node::getMyConfNumber() {
  return myConfNumber;
}

void Node::initNextRound(const PublicKey& mainNode, std::vector<PublicKey>&& confidantNodes) {
  ++roundNum_;
  size_t nTrusted = confidantNodes.size();  //-1;
  mainNode_       = myPublicKey_;           // confidantNodes.at(i);
  confidantNodes_.clear();
  for (auto& conf : confidantNodes)
    confidantNodes_.push_back(conf);

  sendRoundTable();
#ifdef MYLOG
  std::cout << "NODE> RoundNumber :" << roundNum_ << std::endl;
#endif

  onRoundStart();
}

Node::MessageActions Node::chooseMessageAction(const RoundNum rNum, const MsgTypes type) {
  if (type == MsgTypes::BigBang && rNum > getBlockChain().getLastWrittenSequence())
    return MessageActions::Process;
  if (type == MsgTypes::RoundTableRequest)
    return (rNum < roundNum_ ? MessageActions::Process : MessageActions::Drop);
  if (type == MsgTypes::RoundTable)
    return (rNum > roundNum_ ? MessageActions::Process : MessageActions::Drop);
  if (type == MsgTypes::BlockRequest || type == MsgTypes::RequestedBlock)
    return (rNum <= roundNum_ ? MessageActions::Process : MessageActions::Drop);
  if (rNum < roundNum_)
    return type == MsgTypes::NewBlock ? MessageActions::Process : MessageActions::Drop;
  return (rNum == roundNum_ ? MessageActions::Process : MessageActions::Postpone);
}

inline bool Node::readRoundData(const bool tail) {
  PublicKey mainNode;
  uint8_t   confSize = 0;
  istream_ >> confSize;
#ifdef MYLOG
  std::cout << "NODE> Number of confidants :" << (int)confSize << std::endl;
#endif
  if (confSize < MIN_CONFIDANTS || confSize > MAX_CONFIDANTS) {
    LOG_WARN("Bad confidants num");
    return false;
  }

  std::vector<PublicKey> confidants;
  confidants.reserve(confSize);

  istream_ >> mainNode;
  // LOG_EVENT("SET MAIN " << byteStreamToHex(mainNode.str, 32));
  while (istream_) {
    confidants.push_back(PublicKey());
    istream_ >> confidants.back();

    // LOG_EVENT("ADDED CONF " << byteStreamToHex(confidants.back().str, 32));

    if (confidants.size() == confSize && !istream_.end()) {
      if (tail)
        break;
      else {
        LOG_WARN("Too many confidant nodes received");
        return false;
      }
    }
  }

  if (!istream_.good() || confidants.size() < confSize) {
    LOG_WARN("Bad round table format, ignoring");
    return false;
  }

  std::swap(confidants, confidantNodes_);
#ifdef MYLOG
  std::cout << "NODE> RoundNumber :" << roundNum_ << std::endl;
#endif

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
  ostream_.insertBytes((const char*)memPtr.get(), memPtr.size());
}
