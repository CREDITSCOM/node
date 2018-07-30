//
// Created by alexraag on 04.05.2018.
//

#include <iostream>
#include <random>
#include <sstream>

#include <sys/timeb.h>

#include <csdb/address.h>
#include <csdb/pool.h>
#include <csdb/currency.h>
#include <csdb/wallet.h>

#include "Solver/Fake/Fake_Generals.hpp"
#include "Solver/Fake/Fake_Solver.hpp"
#include <algorithm>

#include <lib/system/logger.hpp>

namespace Credits {
using ScopedLock = std::lock_guard<std::mutex>;
constexpr short min_nodes = 3;

Fake_Solver::Fake_Solver(Node* node)
  : ISolver()
  , node_(node)
  , generals(std::unique_ptr<Fake_Generals>(new Fake_Generals()))
  , vector_datas()
  , m_pool()
{
}

Fake_Solver::~Fake_Solver()
{
  //		csconnector::stop();
  //		csstats::stop();
}

static void
addTimestampToPool(csdb::Pool& pool)
{
  struct timeb t;
  ftime(&t);
  pool.add_user_field(
    0, std::to_string((uint64_t)((uint64_t)(t.time) * 1000ll) + t.millitm));
}

inline void runAfter(const std::chrono::milliseconds& ms, std::function<void()> cb) {
  const auto tp = std::chrono::system_clock::now() + ms;
  std::thread tr([tp, cb]() {
                   std::this_thread::sleep_until(tp);
                   LOG_WARN("Inserting callback");
                   CallsQueue::instance().insert(cb);
                 });
  tr.detach();
}

void
Fake_Solver::closeMainRound()
{
  if (m_pool.transactions_count() > 0) {
    m_pool_closed = true;
    node_->sendTransactionList(m_pool, node_->getConfidants()[2]); // Lifehack
  } else {
    node_->becomeWriter();
    addTimestampToPool(m_pool);

#ifdef SPAM_MAIN
    createSpam = false;
    spamThread.join();
    node_->sendBlock(testPool);
#else
    node_->sendBlock(m_pool);
#endif
    node_->getBlockChain().writeLastBlock(m_pool);
  }
}

void
Fake_Solver::runMainRound()
{
  m_pool_closed = false;
  runAfter(std::chrono::milliseconds(100),
           [this]() { closeMainRound(); });
}

void
Fake_Solver::flushTransactions()
{
  {
    std::lock_guard<std::mutex> l(m_trans_mut);
    if (m_transactions.size()) {
      node_->sendTransaction(std::move(m_transactions));
      sentTransLastRound = true;
      m_transactions.clear();
    }
  }
}

void
Fake_Solver::gotTransaction(csdb::Transaction&& transaction)
{
  if (!m_pool_closed) {
    if (transaction.is_valid()) {
      if (transaction.balance() >= transaction.amount()) {
        transaction.set_balance(transaction.balance() - transaction.amount());
        if (m_pool.transactions_count() == 0)
          node_->sendFirstTransaction(transaction);

        m_pool.add_transaction(std::move(transaction));
      }
      // else
      //	LOG_EVENT("Bad balance: " << transaction.balance().integral() <<
      //" . " << transaction.balance().fraction() << " vs. " <<
      // transaction.amount().integral() << " . " <<
      // transaction.amount().fraction());
    } else {
      // LOG_EVENT("Invalid transaction received");
    }
  } else {
    // LOG_EVENT("m_pool_closed already, cannot accept your transactions");
  }
}

void
Fake_Solver::gotTransactionList(csdb::Transaction&& transaction)
{
  std::string result = generals->buildvector(transaction);

  receivedVec_ips.insert(node_->getMyPublicKey());
  generals->addvector(result);

  node_->sendVector(result);
}

void
Fake_Solver::gotVector(Vector&& vector, const PublicKey& general)
{
  bool found = false;

  for (auto& it : node_->getConfidants()) {
    if (it == general) // One of confidant
    {
      for (auto& rec_ips : receivedVec_ips) {
        if (rec_ips == general) // Ignore that one
        {
          found = true;
          break;
        }
      }
      if (!found) {
        receivedVec_ips.insert(general);
        generals->addvector(vector);
      }
      break;
    }
  }

  if (receivedVec_ips.size() >= node_->getConfidants().size()) {
    vectorComplete = true;

    receivedVec_ips.clear();
    receivedMat_ips.insert(node_->getMyPublicKey());

    generals->addmatrix(generals->getMatrix());

    node_->sendMatrix(generals->getMatrix());
  }
}

void
Fake_Solver::gotMatrix(Matrix&& matrix, const PublicKey& general)
{
  bool found = false;

  for (auto& it : node_->getConfidants()) {
    if (it == general) {
      for (auto& rec_ips : receivedMat_ips) {
        if (rec_ips == general) {
          found = true;
          break;
        }
      }
      if (!found) {
        receivedMat_ips.insert(general);
        if (generals != nullptr)
          generals->addmatrix(matrix);
      }
      break;
    }
  }

  if (receivedMat_ips.size() >= node_->getConfidants().size()) {
    vectorComplete = false;

    int status =
      generals->take_desision(node_->getConfidants(), node_->getMyPublicKey());
    receivedMat_ips.clear();

    if (status == 2) {
      node_->becomeWriter();

      consensusAchieved = true;
      writeNewBlock();
    } else {
    }
  }
}

void
Fake_Solver::writeNewBlock()
{
  if (consensusAchieved && blockCandidateArrived &&
      node_->getMyLevel() == NodeLevel::Writer) {
    addTimestampToPool(m_pool);
    node_->sendBlock(m_pool);
    node_->getBlockChain().writeLastBlock(m_pool);
    consensusAchieved = false;
  }
}

void
Fake_Solver::gotBlock(csdb::Pool&& block, const PublicKey& sender)
{
#ifdef MONITOR_NODE
  addTimestampToPool(block);
#endif
  node_->getBlockChain().writeLastBlock(block);

#ifdef MONITOR_NODE
  node_->getConnector().getApi()->logNewBlock(block);
#endif

#ifndef SPAMMER
#ifndef MONITOR_NODE
  if (!sentTransLastRound) {
    Hash test_hash = "zpa02824qsltp";
    node_->sendHash(test_hash, sender);
  }
#endif
#endif
}

void
Fake_Solver::gotBlockCandidate(csdb::Pool&& block)
{
  if (blockCandidateArrived)
    return;

  m_pool = std::move(block);

  blockCandidateArrived = true;
  writeNewBlock();
}

void
Fake_Solver::gotHash(Hash&& hash, const PublicKey& sender)
{
  if (hashes.size() < min_nodes) {
    hashes.push_back(hash);
    ips.push_back(sender);
  } else
    return;

  if (hashes.size() == min_nodes) {
    node_->initNextRound(node_->getMyPublicKey(), std::move(ips));
    node_->sendRoundTable();
  }
}

void
Fake_Solver::initApi()
{
  _initApi();
}

#ifdef STARTER
void Fake_Solver::startRounds() {
  ips.clear();
  for (int i = 0; i < 3; ++i)
    ips.push_back(node_->getMyPublicKey());

  node_->becomeWriter();
  node_->initNextRound(node_->getMyPublicKey(), std::move(ips));
  node_->sendRoundTable();
}
#endif

void
Fake_Solver::_initApi()
{
  //        csconnector::start(&(node_->getBlockChain()),csconnector::Config{});
  //
  //		csstats::start(&(node_->getBlockChain()));
}

  /////////////////////////////

#ifdef SPAM_MAIN
static int
randFT(int min, int max)
{
  return rand() % (max - min + 1) + min;
}

void
Fake_Solver::createPool()
{
  std::string mp = "0123456789abcdef";
  const unsigned int cmd = 6;

  struct timeb tt;
  ftime(&tt);
  srand(tt.time * 1000 + tt.millitm);

  std::string aStr(64, '0');
  std::string bStr(64, '0');

  static csdb::Pool pizd;
  static bool pizdFlag = false;
  if (!pizdFlag) {
    pizdFlag = true;

    csdb::Transaction transaction;
    transaction.set_currency(csdb::Currency("CS"));

    for (size_t i = 0; i < 64; ++i) {
      aStr[i] = mp[randFT(0, 15)];
      bStr[i] = mp[randFT(0, 15)];
    }

    transaction.set_target(csdb::Address::from_string(aStr));
    transaction.set_source(csdb::Address::from_string(bStr));

    transaction.set_amount(csdb::Amount(1, 0));
    transaction.set_balance(csdb::Amount(2, 0));

    for (uint32_t i = 0; i < 350000; ++i) {
      //Transaction.set_amount(csdb::Amount(randFT(1, 1000), 0));
      //t.set_balance(csdb::Amount(t.balance().integral() + 1, 0));
      pizd.add_transaction(transaction);
    }
  }

  testPool = pizd;

  /*std::string aStr(64, '0');
    std::string bStr(64, '0');*/

  uint32_t limit = randFT(200000, 350000);
  testPool.transactions().resize(limit);

  //t.set_amount(csdb::Amount(randFT(1, 1000), 0));
  //t.set_balance(csdb::Amount(t.balance().integral() + 1, 0));

  //for (auto& t : testPool.transactions()) {
  //}


  /*if (randFT(0, 150) == 42) {
    csdb::Transaction smart_trans;
    smart_trans.set_currency(csdb::Currency("CS"));

    smart_trans.set_target(Credits::BlockChain::getAddressFromKey(
      "3SHCtvpLkBWytVSqkuhnNk9z1LyjQJaRTBiTFZFwKkXb"));
    smart_trans.set_source(csdb::Address::from_string(
      "0000000000000000000000000000000000000000000000000000000000000001"));

    smart_trans.set_amount(csdb::Amount(1, 0));
    smart_trans.set_balance(csdb::Amount(100, 0));

    api::SmartContract sm;
    sm.address = "3SHCtvpLkBWytVSqkuhnNk9z1LyjQJaRTBiTFZFwKkXb";
    sm.method = "store_sum";
    sm.params = { "123", "456" };

    smart_trans.add_user_field(0, serialize(sm));

    testPool.add_transaction(smart_trans);
    }*/

  /*csdb::Transaction transaction;
  transaction.set_currency(csdb::Currency("CS"));

      for (size_t i = 0; i < 64; ++i) {
      aStr[i] = mp[randFT(0, 15)];
      bStr[i] = mp[randFT(0, 15)];
    }

    transaction.set_target(csdb::Address::from_string(aStr));
    transaction.set_source(csdb::Address::from_string(bStr));

  while (createSpam && limit > 0) {
    transaction.set_amount(csdb::Amount(randFT(1, 1000), 0));
    transaction.set_balance(
      csdb::Amount(transaction.balance().integral() + 1, 0));

    testPool.add_transaction(transaction);
    --limit;
    }*/

  addTimestampToPool(testPool);
}
#endif

#ifdef SPAMMER

static inline int
randFT(int min, int max)
{
  return rand() % (max - min) + min;
}

void
Fake_Solver::spamWithTransactions()
{
  std::string mp = "1234567890abcdef";

  // std::string cachedBlock;
  // cachedBlock.reserve(64000);

  std::this_thread::sleep_for(std::chrono::seconds(5));

  auto aaa = csdb::Address::from_string(
    "0000000000000000000000000000000000000000000000000000000000000001");
  auto bbb = csdb::Address::from_string(
    "0000000000000000000000000000000000000000000000000000000000000002");

  csdb::Transaction transaction;
  transaction.set_target(aaa);
  transaction.set_source(bbb);

  transaction.set_currency(csdb::Currency("CS"));

  while (true) {
    if (spamRunning) {
      {
        transaction.set_amount(csdb::Amount(randFT(1, 1000), 0));
        transaction.set_balance(
          csdb::Amount(transaction.amount().integral() + 1, 0));

        std::lock_guard<std::mutex> l(m_trans_mut);
        m_transactions.push_back(transaction);
      }
    }

    std::this_thread::sleep_for(std::chrono::microseconds(10));
  }
}
#endif

///////////////////

void
Fake_Solver::send_wallet_transaction(const csdb::Transaction& transaction)
{
  std::lock_guard<std::mutex> l(m_trans_mut);
  m_transactions.push_back(transaction);
}

void
Fake_Solver::addInitialBalance()
{
  std::cout << "===SETTING DB===" << std::endl;
  const std::string start_address =
    "0000000000000000000000000000000000000000000000000000000000000002";

  csdb::Pool pool;
  csdb::Transaction transaction;
  csdb::internal::byte_array barr(node_->getMyPublicKey().str, node_->getMyPublicKey().str + 32);
  transaction.set_target(
                         csdb::Address::from_public_key(barr));
  transaction.set_source(csdb::Address::from_string(start_address));

  transaction.set_currency(csdb::Currency("CS"));
  transaction.set_amount(csdb::Amount(10000, 0));
  transaction.set_balance(csdb::Amount(100, 0));

  if (!pool.add_transaction(transaction))
    LOG_ERROR("Initial transaction is not valid");

  addTimestampToPool(pool);
  node_->getBlockChain().writeLastBlock(pool);

#ifdef SPAMMER
  spamThread = std::thread(&Fake_Solver::spamWithTransactions, this);
  spamThread.detach();
#endif

#ifdef STARTER
  auto self = this;
  runAfter(std::chrono::milliseconds(5000), [self] () {
                                              LOG_WARN("GGG is " << self);
                                              self->startRounds();
                                            });
#endif
}

void
Fake_Solver::nextRound()
{
  receivedVec_ips.clear();
  receivedMat_ips.clear();

  hashes.clear();
  ips.clear();
  vector_datas.clear();

  vectorComplete = false;
  consensusAchieved = false;
  blockCandidateArrived = false;

  sentTransLastRound = false;

  m_pool = csdb::Pool{};
  if (node_->getMyLevel() == NodeLevel::Main) {
    runMainRound();
#ifdef SPAM_MAIN
    createSpam = true;
    spamThread = std::thread(&Fake_Solver::createPool, this);
#endif
#ifdef SPAMMER
    spamRunning = false;
#endif
  } else {
#ifdef SPAMMER
    spamRunning = true;
#endif
    m_pool_closed = true;
    flushTransactions();
  }
}

// Needed for dataBase
void
Fake_Solver::sign_transaction(const void* buffer, const size_t buffer_size)
{}
void
Fake_Solver::verify_transaction(const void* buffer, const size_t buffer_size)
{}
}
