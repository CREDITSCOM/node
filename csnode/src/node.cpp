#include <Solver/ISolver.hpp>
#include <Solver/SolverFactory.hpp>

#include <csnode/node.hpp>
#include <lib/system/logger.hpp>
#include <net/transport.hpp>

#include <base58.h>
#include <sodium.h>

#include <snappy/snappy.h>

const unsigned MIN_CONFIDANTS = 3;
const unsigned MAX_CONFIDANTS = 3;

Node::Node(const Config& config):
  myPublicKey_(config.getMyPublicKey()),
  bc_(config.getPathToDB().c_str()),
  solver_(Credits::SolverFactory().createSolver(Credits::solver_type::fake, this)),
  transport_(new Transport(config, this)),
  stats_(bc_),
  api_(bc_, solver_),
  allocator_(1 << 24, 5),
  ostream_(&allocator_, myPublicKey_) {
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
 // solver_->set_keys(myPublicForSig, myPrivateForSig); //DECOMMENT WHEN SOLVER STRUCTURE WILL BE CHANGED!!!!

  solver_->addInitialBalance();

  return true;
}


bool Node::checkKeysFile()
{
  std::ifstream pub("NodePublic.txt"); //44
  std::ifstream priv("NodePrivate.txt"); //88
  if (!pub.is_open() || !priv.is_open())
  {
    std::cout << "\n\nNo suitable keys were found. Type \"g\" to generate or \"q\" to quit." << std::endl;
    char gen_flag = 'a';
    std::cin >> gen_flag;
    if (gen_flag == 'g')
    {
      generateKeys();
      return true;
    }
    else return false;
  }
  else
  {
    std::string pub58, priv58;
    std::getline(pub, pub58);
    std::getline(priv, priv58);
    pub.close();
    priv.close();
    DecodeBase58(pub58, myPublicForSig);
    DecodeBase58(priv58, myPrivateForSig);
    if (myPublicForSig.size() != 32 || myPrivateForSig.size() != 64)
    {
      std::cout << "\n\nThe size of keys found is not correct. Type \"g\" to generate or \"q\" to quit." << std::endl;
      char gen_flag = 'a';
      std::cin >> gen_flag;
      if (gen_flag == 'g')
      {
        generateKeys();
        return true;
      }
      else return false;
    }
    if (checkKeysForSig())
      return true;
    else return false;
  }
}


void Node::generateKeys()
{
  uint8_t private_key[64], public_key[32];
  crypto_sign_ed25519_keypair(public_key, private_key);
  myPublicForSig.clear();
  myPrivateForSig.clear();
  for (int i = 0; i < 32; i++)
    myPublicForSig.push_back(public_key[i]);

  for (int i = 0; i < 64; i++)
    myPrivateForSig.push_back(private_key[i]);

  std::string pub58, priv58;
  pub58 = EncodeBase58(myPublicForSig);
  priv58 = EncodeBase58(myPrivateForSig);

  std::ofstream f_pub("NodePublic.txt");
  f_pub << pub58;
  f_pub.close();

  std::ofstream f_priv("NodePrivate.txt");
  f_priv << priv58;
  f_priv.close();
}

bool Node::checkKeysForSig()
{
  const uint8_t msg[] = { 255, 0, 0, 0, 255 };
  uint8_t signature[64], public_key[32], private_key[64];
  for (int i = 0; i < 32; i++)
    public_key[i] = myPublicForSig[i];
  for (int i = 0; i < 64; i++)
    private_key[i] = myPrivateForSig[i];
  uint64_t sig_size;
  crypto_sign_ed25519_detached(signature, reinterpret_cast<unsigned long long *>(&sig_size), msg, 5, private_key);
  int ver_ok = crypto_sign_ed25519_verify_detached(signature, msg, 5, public_key);
  if (ver_ok == 0)
    return true;
  else
  {
    std::cout << "\n\nThe keys for node are not correct. Type \"g\" to generate or \"q\" to quit." << std::endl;
    char gen_flag = 'a';
    std::cin >> gen_flag;
    if (gen_flag == 'g')
    {
      generateKeys();
      return true;
    }
    else return false;
  }
}

void Node::run(const Config&) {
  transport_->run();
}

/* Requests */

void Node::flushCurrentTasks() {
  transport_->addTask(ostream_.getPackets(),
                      ostream_.getPacketsCount());
  ostream_.clear();
}

void Node::getRoundTable(const uint8_t* data, const size_t size, const RoundNum rNum) {
  istream_.init(data, size);

  if (!readRoundData(false))
    return;

  roundNum_ = rNum;

  if (!istream_.good()) {
    LOG_WARN("Bad round table format, ignoring");
    return;
  }

  onRoundStart();
  transport_->clearTasks();

  transport_->processPostponed(rNum);
}

void Node::sendRoundTable() {
  ostream_.init(BaseFlags::Broadcast);
  ostream_ << MsgTypes::RoundTable
           << roundNum_
           << static_cast<uint8_t>(confidantNodes_.size())
           << mainNode_;

  for (auto& conf : confidantNodes_)
    ostream_ << conf;

  LOG_EVENT("Sending round table");

  transport_->clearTasks();
  flushCurrentTasks();
}

void Node::getTransaction(const uint8_t* data, const size_t size) {
  if (myLevel_ != NodeLevel::Main &&
      myLevel_ != NodeLevel::Writer) {
    return;
  }

  istream_.init(data, size);

  while (istream_.good() && !istream_.end()) {
    csdb::Transaction trans;
    istream_ >> trans;
    solver_->gotTransaction(std::move(trans));
  }

  if (!istream_.good()) {
    LOG_WARN("Bad transaction packet format");
    return;
  }
}

void Node::sendTransaction(const csdb::Transaction& trans) {
  ostream_.init(BaseFlags::Broadcast, mainNode_);
  ostream_ << MsgTypes::Transactions
           << roundNum_
           << trans;

  LOG_EVENT("Sending transaction");
  flushCurrentTasks();
}

void Node::sendTransaction(std::vector<csdb::Transaction>&& transactions) {
  ostream_.init(BaseFlags::Broadcast | BaseFlags::Fragmented, mainNode_);
  ostream_ << MsgTypes::Transactions << roundNum_;

  for (auto& tr : transactions)
    ostream_ << tr;

  LOG_EVENT("Sending transactions");
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
    LOG_WARN("Bad transaction packet format");
    return;
  }

  LOG_EVENT("Got first transaction, initializing consensus...");
  solver_->gotTransactionList(std::move(trans));
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
  if (myLevel_ != NodeLevel::Confidant && myLevel_ != NodeLevel::Writer) {
    return;
  }

  istream_.init(data, size);

  csdb::Pool pool;
  istream_ >> pool;

  if (!istream_.good() || !istream_.end()) {
    LOG_WARN("Bad transactions list packet format");
    return;
  }

  LOG_EVENT("Got full transactions list of " << pool.transactions_count());
  solver_->gotBlockCandidate(std::move(pool));
}

void Node::sendTransactionList(const csdb::Pool& pool, const PublicKey& target) {
  if (myLevel_ != NodeLevel::Main) {
    LOG_ERROR("Only main nodes can send transaction lists");
    return;
  }

  ostream_.init(BaseFlags::Fragmented, target);
  ostream_ << MsgTypes::TransactionList
           << roundNum_
           << pool;

  flushCurrentTasks();
}

void Node::getVector(const uint8_t* data, const size_t size, const PublicKey& sender) {
  if (myLevel_ != NodeLevel::Confidant) {
    return;
  }

  istream_.init(data, size);

  Vector vec;
  istream_ >> vec;

  if (!istream_.good() || !istream_.end()) {
    LOG_WARN("Bad vector packet format");
    return;
  }

  LOG_EVENT("Got vector");
  solver_->gotVector(std::move(vec), sender);
}

void Node::sendVector(const Vector& vector) {
  if (myLevel_ != NodeLevel::Confidant) {
    LOG_ERROR("Only confidant nodes can send vectors");
    return;
  }

  ostream_.init(BaseFlags::Signed);
  ostream_ << MsgTypes::ConsVector << roundNum_ << vector;

  flushCurrentTasks();
}

void Node::getMatrix(const uint8_t* data, const size_t size, const PublicKey& sender) {
  if (myLevel_ != NodeLevel::Confidant) {
    return;
  }

  istream_.init(data, size);

  Matrix mat;
  istream_ >> mat;

  if (!istream_.good() || !istream_.end()) {
    LOG_WARN("Bad matrix packet format");
    return;
  }

  LOG_EVENT("Got matrix");
  solver_->gotMatrix(std::move(mat), sender);
}

void Node::sendMatrix(const Matrix& matrix) {
  if (myLevel_ != NodeLevel::Confidant) {
    LOG_ERROR("Only confidant nodes can send matrices");
    return;
  }

  ostream_.init(BaseFlags::Signed);
  ostream_ << MsgTypes::ConsMatrix << roundNum_ << matrix;

  flushCurrentTasks();
}


uint32_t Node::getRoundNumber()
{
  return roundNum_;
}

void Node::getBlock(const uint8_t* data, const size_t size, const PublicKey& sender) {
  if (myLevel_ == NodeLevel::Writer) {
    LOG_WARN("Writer cannot get blocks");
    return;
  }

  myLevel_ = NodeLevel::Normal;
  istream_.init(data, size);

  csdb::Pool pool;
  istream_ >> pool;

  if (!istream_.good() || !istream_.end()) {
    LOG_WARN("Bad block packet format");
    return;
  }

  LOG_EVENT("Got block of " << pool.transactions_count());

  solver_->gotBlock(std::move(pool), sender);
}

void Node::sendBlock(const csdb::Pool& pool) {
  if (myLevel_ != NodeLevel::Writer) {
    LOG_ERROR("Only writer nodes can send blocks");
    return;
  }

  ostream_.init(BaseFlags::Broadcast | BaseFlags::Fragmented | BaseFlags::Compressed);
  size_t bSize;
  const void* data = const_cast<csdb::Pool&>(pool).to_byte_stream(bSize);

  std::string compressed;
  snappy::Compress((const char*)data, bSize, &compressed);
  ostream_ << MsgTypes::NewBlock
           << roundNum_
           << compressed;

  LOG_EVENT("Sending block of " << pool.transactions_count());
  flushCurrentTasks();
}

void Node::getHash(const uint8_t* data, const size_t size, const PublicKey& sender) {
  if (myLevel_ != NodeLevel::Writer) {
    return;
  }

  istream_.init(data, size);

  Hash hash;
  istream_ >> hash;

  if (!istream_.good() || !istream_.end()) {
    LOG_WARN("Bad hash packet format");
    return;
  }

  LOG_EVENT("Got hash");
  solver_->gotHash(std::move(hash), sender);
}

void Node::sendHash(const Hash& hash, const PublicKey& target) {
  if (myLevel_ == NodeLevel::Writer) {
    LOG_ERROR("Writer node shouldn't send hashes");
    return;
  }

  LOG_WARN("Sending hash");

  ostream_.init(BaseFlags::Signed | BaseFlags::Encrypted, target);
  ostream_ << MsgTypes::BlockHash
           << roundNum_
           << hash;
  flushCurrentTasks();
}


void Node::getBlockRequest(const uint8_t* data, const size_t size, const PublicKey& sender) {
  if (myLevel_ != NodeLevel::Normal)
    return;
  if (sender == myPublicKey_) return;
  uint32_t requested_seq;
  istream_.init(data, size);
  istream_ >> requested_seq;
  std::cout << "GETBLOCKREQUEST> Getting the request for block: " << requested_seq << std::endl;
  if (requested_seq > getBlockChain().getLastWrittenSequence())
  {
    std::cout << "GETBLOCKREQUEST> The requested block: " << requested_seq << " is BEYOND my CHAIN" << std::endl;
    return;
  }
  //REMOVE COMMENT BEFORE USE
  //solver_->gotBlockRequest(std::move(getBlockChain().getHashBySequence(requested_seq)), sender);
}

void Node::sendBlockRequest(uint32_t seq) {
  if (awaitingSyncroBlock)
  {
    std::cout << "SENDBLOCKREQUEST> New request won't be sent, we're awaiting block:  " << sendBlockRequestSequence << std::endl;
    return;
  }

  //ostream_.init();
  //ostream_ << seq;
 
  //Packet* pack;
  
 // transport_->sendBroadcast()
  sendBlockRequestSequence = seq;
  awaitingSyncroBlock = true;
  ostream_.init(BaseFlags::Broadcast);
  ostream_ << MsgTypes::RequestedBlock
    << seq;
  flushCurrentTasks();
  
  std::cout << "SENDBLOCKREQUEST> Sending request for block: " << seq << std::endl;
}

void Node::getBlockReply(const uint8_t* data, const size_t size) {
  csdb::Pool pool;

  istream_.init(data, size);
  istream_ >> pool;
  std::cout << "GETBLOCKREPLY> Getting block" << std::endl;
  if (pool.sequence() == sendBlockRequestSequence)
  {
    std::cout << "GETBLOCKREPLY> Block Sequence is Ok" << std::endl;

//    solver_->gotBlockReply(std::move(pool));
    awaitingSyncroBlock = false;
  }
  if (getBlockChain().getGlobalSequence() >= getBlockChain().getLastWrittenSequence())
    sendBlockRequest(getBlockChain().getLastWrittenSequence() + 1);
  else
  {
    syncro_started = false;
    std::cout << "SYNCRO FINISHED!!!" << std::endl;
  }

}

void Node::sendBlockReply(const csdb::Pool& pool, const  PublicKey& sender) {
 
   std::cout << "SENDBLOCKREPLY> Sending block to " << sender.str << std::endl;
   ostream_.init(BaseFlags::Signed, sender);
   ostream_ << MsgTypes::RequestedBlock
      << pool;
    flushCurrentTasks();
  }

void Node::becomeWriter() {
  //if (myLevel_ != NodeLevel::Main && myLevel_ != NodeLevel::Confidant)
  //  LOG_WARN("Logically impossible to become a writer right now");

  myLevel_ = NodeLevel::Writer;
}

void Node::onRoundStart() {
  if (mainNode_ == myPublicKey_)
    myLevel_ = NodeLevel::Main;
  else {
    bool found = false;
    for (auto& conf : confidantNodes_) {
      if (conf == myPublicKey_) {
        myLevel_ = NodeLevel::Confidant;
        found = true;
        break;
      }
    }

    if (!found)
      myLevel_ = NodeLevel::Normal;
  }

  solver_->nextRound();

  // Pretty printing...
  std::cerr << "Round " << roundNum_ << " started. Mynode_type:=" << myLevel_
            << ", General: " << byteStreamToHex(mainNode_.str, 32) << ", Confidants: ";
  for (auto& e : confidantNodes_)
    std::cerr << byteStreamToHex(e.str, 32) << " ";

  std::cerr << std::endl;

  transport_->processPostponed(roundNum_);
}

void Node::initNextRound(const PublicKey& mainNode, std::vector<PublicKey>&& confidantNodes) {
  /*if (myLevel_ != NodeLevel::Writer) {
    LOG_ERROR(
              "Trying to initialize a new round without the required privileges");
    return;
    }*/

  ++roundNum_;
  mainNode_ = mainNode;
  std::swap(confidantNodes, confidantNodes_);
  onRoundStart();
}

Node::MessageActions Node::chooseMessageAction(const RoundNum rNum, const MsgTypes type) {
  if (rNum == roundNum_ ||
      type == MsgTypes::NewBlock)
    return MessageActions::Process;

  if (rNum < roundNum_)
    return MessageActions::Drop;

  return (type == MsgTypes::RoundTable ? MessageActions::Process : MessageActions::Postpone);
}

inline bool Node::readRoundData(const bool tail) {
  PublicKey mainNode;

  uint8_t confSize = 0;
  istream_ >> confSize;

  if (confSize < MIN_CONFIDANTS || confSize > MAX_CONFIDANTS) {
    LOG_WARN("Bad confidants num");
    return false;
  }

  std::vector<PublicKey> confidants;
  confidants.reserve(confSize);

  istream_ >> mainNode;
  //LOG_EVENT("SET MAIN " << byteStreamToHex(mainNode.str, 32));
  while (istream_) {
    confidants.push_back(PublicKey());
    istream_ >> confidants.back();

    //LOG_EVENT("ADDED CONF " << byteStreamToHex(confidants.back().str, 32));

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

  mainNode_ = mainNode;
  std::swap(confidants, confidantNodes_);

  return true;
}
