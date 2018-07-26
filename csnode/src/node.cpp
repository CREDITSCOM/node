#include <Solver/ISolver.hpp>
#include <Solver/SolverFactory.hpp>

#include <csnode/node.hpp>
#include <lib/system/logger.hpp>
#include <net/transport.hpp>

#include <snappy/snappy.h>

const unsigned MIN_CONFIDANTS = 3;
const unsigned MAX_CONFIDANTS = 3;

Node::Node(const Config& config):
  myPublicKey_(config.getMyPublicKey()),
  bc_(config.getPathToDB().c_str()),
  solver_(Credits::SolverFactory().createSolver(Credits::solver_type::fake, this)),
  transport_(new Transport(config, this)),
  allocator_(1 << 24, 5),
  ostream_(&allocator_, myPublicKey_),
  stats_(bc_),
  api_(bc_, solver_) {
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

  //solver_->initApi();
  solver_->addInitialBalance();

  return true;
}

void Node::run(const Config& config) {
  transport_->run(config);
}

/* Requests */

void Node::flushCurrentTasks() {
  transport_->addTask(ostream_.getPackets(),
                      ostream_.getPacketsCount());
  ostream_.clear();
}

void Node::getRoundTable(const uint8_t* data, const size_t size) {
  istream_.init(data, size);
  LOG_WARN("got msg of size " << size);

  if (!readRoundData(false))
    return;

  if (!istream_.good()) {
    LOG_WARN("Bad round table format, ignoring");
    return;
  }

  onRoundStart();
  transport_->clearTasks();
}

void Node::sendRoundTable() {
  ostream_.init(BaseFlags::Broadcast);
  ostream_ << MsgTypes::RoundTable
           << roundNum_
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
           << trans;

  LOG_EVENT("Sending transaction");
  flushCurrentTasks();
}

void Node::sendTransaction(std::vector<csdb::Transaction>&& transactions) {
  ostream_.init(BaseFlags::Broadcast | BaseFlags::Fragmented, mainNode_);
  ostream_ << MsgTypes::Transactions;

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
  ostream_ << MsgTypes::FirstTransaction << trans;

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
  ostream_ << MsgTypes::ConsVector << vector;

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
  ostream_ << MsgTypes::ConsMatrix << matrix;

  flushCurrentTasks();
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
           << hash;
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

inline bool Node::readRoundData(const bool tail) {
  uint32_t roundNum;
  PublicKey mainNode;
  std::vector<PublicKey> confidants;
  confidants.reserve(MAX_CONFIDANTS);

  istream_ >> roundNum;
  if (roundNum <= roundNum_) {
    LOG_NOTICE("Old round info received: " << roundNum_ << " >= " << roundNum
               << ". Ignoring...");
    return false;
  }

  istream_ >> mainNode;
  //LOG_EVENT("SET MAIN " << byteStreamToHex(mainNode.str, 32));
  while (istream_) {
    confidants.push_back(PublicKey());
    istream_ >> confidants.back();

    //LOG_EVENT("ADDED CONF " << byteStreamToHex(confidants.back().str, 32));

    if (confidants.size() == MAX_CONFIDANTS && !istream_.end()) {
      if (tail)
        break;
      else {
        LOG_WARN("Too many confidant nodes received");
        return false;
      }
    }
  }

  if (!istream_.good() || confidants.size() < MIN_CONFIDANTS) {
    LOG_WARN("Bad round table format, ignoring");
    return false;
  }

  roundNum_ = roundNum;
  mainNode_ = mainNode;
  std::swap(confidants, confidantNodes_);

  return true;
}
