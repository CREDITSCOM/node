////////////////////////////////////////////////////////////////////////////////////////////////////////////
//                    Created by Analytical Solytions Core Team 07.09.2018                                //
////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include <algorithm>
#include <boost/dynamic_bitset.hpp>

#include <chrono>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>

#include <csdb/address.h>
#include <csdb/currency.h>
#include <csdb/wallet.h>

#include <csnode/node.hpp>

#include <algorithm>
#include <cmath>
#include "Solver/Generals.hpp"
#include "Solver/Solver.hpp"

#include <lib/system/logger.hpp>

#include <base58.h>
#include <sodium.h>

namespace {
void addTimestampToPool(csdb::Pool& pool) {
  auto now_time = std::chrono::system_clock::now();
  pool.add_user_field(
      0, std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(now_time.time_since_epoch()).count()));
}

void runAfter(const std::chrono::milliseconds& ms, std::function<void()> cb) {
  // std::cout << "SOLVER> Before calback" << std::endl;
  const auto  tp = std::chrono::system_clock::now() + ms;
  std::thread tr([tp, cb]() {
    std::this_thread::sleep_until(tp);
    //  LOG_WARN("Inserting callback");
    CallsQueue::instance().insert(cb);
  });
  // std::cout << "SOLVER> After calback" << std::endl;
  tr.detach();
}

#if defined(SPAM_MAIN) || defined(SPAMMER)
static int randFT(int min, int max) {
  return rand() % (max - min + 1) + min;
}
#endif
}  // namespace

namespace cs {
using ScopedLock          = std::lock_guard<std::mutex>;
constexpr short min_nodes = 3;

Solver::Solver(Node* node)
: node_(node)
, generals(std::unique_ptr<Generals>(new Generals()))
, vector_datas()
, m_pool()
, v_pool()
, b_pool() {
  m_SendingPacketTimer.connect(std::bind(&Solver::flushTransactions, this));
}

Solver::~Solver() {
  m_SendingPacketTimer.disconnect();
  m_SendingPacketTimer.stop();
}

void Solver::set_keys(const std::vector<uint8_t>& pub, const std::vector<uint8_t>& priv) {
  myPublicKey  = pub;
  myPrivateKey = priv;
}

void Solver::buildBlock(csdb::Pool& block) {
  csdb::Transaction transaction;

  transaction.set_target(
      csdb::Address::from_string("0000000000000000000000000000000000000000000000000000000000000003"));
  transaction.set_source(
      csdb::Address::from_string("0000000000000000000000000000000000000000000000000000000000000002"));

  transaction.set_currency(csdb::Currency("CS"));
  transaction.set_amount(csdb::Amount(10, 0));
  transaction.set_balance(csdb::Amount(100, 0));
  transaction.set_innerID(0);

  block.add_transaction(transaction);

  transaction.set_target(
      csdb::Address::from_string("0000000000000000000000000000000000000000000000000000000000000004"));
  transaction.set_source(
      csdb::Address::from_string("0000000000000000000000000000000000000000000000000000000000000002"));

  transaction.set_currency(csdb::Currency("CS"));
  transaction.set_amount(csdb::Amount(10, 0));
  transaction.set_balance(csdb::Amount(100, 0));
  transaction.set_innerID(0);

  block.add_transaction(transaction);
}

void Solver::prepareBlockForSend(csdb::Pool& block) {
  // std::cout << "SOLVER> Before time stamp" << std::endl;
  // block is build in buildvector
  addTimestampToPool(block);
  // std::cout << "SOLVER> Before write pub key" << std::endl;
  block.set_writer_public_key(myPublicKey);
  // std::cout << "SOLVER> Before write last sequence" << std::endl;
  block.set_sequence((node_->getBlockChain().getLastWrittenSequence()) + 1);
  csdb::PoolHash prev_hash;
  prev_hash.from_string("");
  block.set_previous_hash(prev_hash);
  // std::cout << "SOLVER> Before private key" << std::endl;
  block.sign(myPrivateKey);
#ifdef MYLOG
  std::cout
      << "last sequence: " << (node_->getBlockChain().getLastWrittenSequence())
      << std::
             endl;  // ", last time:" <<
                    // node_->getBlockChain().loadBlock(node_->getBlockChain().getLastHash()).user_field(0).value<std::string>().c_str()
  std::cout << "prev_hash: " << node_->getBlockChain().getLastHash().to_string() << " <- Not sending!!!" << std::endl;
  std::cout << "new sequence: " << block.sequence() << ", new time:" << block.user_field(0).value<std::string>().c_str()
            << std::endl;
#endif
}

void Solver::sendTL() {
  if (gotBigBang)
    return;
  uint32_t tNum = v_pool.transactions_count();

  std::cout << "AAAAAAAAAAAAAAAAAAAAAAAA -= TRANSACTION RECEIVING IS OFF =- AAAAAAAAAAAAAAAAAAAAAAAAAAAA" << std::endl;
#ifdef MYLOG
  std::cout << "                          Total received " << tNum << " transactions" << std::endl;
#endif
  std::cout << "========================================================================================" << std::endl;

  m_pool_closed = true;

  std::cout << "Solver -> Sending " << tNum << " transactions " << std::endl;

  v_pool.set_sequence(node_->getRoundNumber());
  node_->sendTransactionList(std::move(v_pool));  // Correct sending, better when to all one time
}


uint32_t Solver::getTLsize() {
  return v_pool.transactions_count();
}

void Solver::setLastRoundTransactionsGot(size_t trNum) {
  lastRoundTransactionsGot = trNum;
}

void Solver::applyCharacteristic(const std::vector<uint8_t>& characteristic, uint32_t bitsCount,
                                 const csdb::Pool& metaInfoPool, const PublicKey& sender) {
  cslog() << "SOLVER> ApplyCharacteristic";
  if (node_->getMyLevel() == NodeLevel::Writer)
    return;
  gotBigBang        = false;
  gotBlockThisRound = true;
  uint64_t sequence = metaInfoPool.sequence();
  cslog() << "SOLVER> ApplyCharacteristic : sequence = " << sequence;
  std::string timestamp = metaInfoPool.user_field(0).value<std::string>();

  boost::dynamic_bitset<> mask{characteristic.begin(), characteristic.end()};

  size_t            maskIndex = 0;
  const cs::Hashes& hashes    = mRound.hashes;

  cs::SharedLock sharedLock(mSharedMutex);

  for (const auto& hash : hashes) {
    auto        it           = mHashTable.find(hash);
    const auto& transactions = it->second.transactions();

    if (it == mHashTable.end()) {
      cserror() << "HASH NOT FOUND";
      return;
    }

    if (bitsCount != transactions.size()) {
      cserror() << "MASK SIZE AND TRANSACTIONS HASH COUNT - MUST BE EQUAL";
      return;
    }

    for (const auto& transaction : transactions) {
      if (mask.test(maskIndex)) {
        m_pool.add_transaction(transaction);
      }

      ++maskIndex;
    }
    mHashTable.erase(it);
  }

  m_pool.set_sequence(sequence);
  m_pool.add_user_field(0, timestamp);
  cslog() << "SOLVER> ApplyCharacteristic: pool created";
#ifdef MONITOR_NODE
  addTimestampToPool(m_pool);
#endif
  uint32_t g_seq = m_pool.sequence();
#ifdef MYLOG
  std::cout << "GOT NEW BLOCK: global sequence = " << g_seq << std::endl;
#endif
  if (g_seq > node_->getRoundNumber())
    return;  // remove this line when the block candidate signing of all trusted will be implemented

  node_->getBlockChain().setGlobalSequence(g_seq);
  if (g_seq == node_->getBlockChain().getLastWrittenSequence() + 1) {
    node_->getBlockChain().putBlock(m_pool);
#ifndef MONITOR_NODE
    if ((node_->getMyLevel() != NodeLevel::Writer) && (node_->getMyLevel() != NodeLevel::Main)) {
      Hash test_hash((char*)(node_->getBlockChain().getLastWrittenHash().to_binary().data()));  // getLastWrittenHash().to_binary().data()));//SENDING
                                                                                                // HASH!!!
      node_->sendHash(test_hash, sender);
#ifdef MYLOG
      std::cout << "SENDING HASH: " << byteStreamToHex(test_hash.str, 32) << std::endl;
#endif
    }
#endif
    //}
  }
}
void Solver::closeMainRound() {
  if (node_->getRoundNumber() == 1)  // || (lastRoundTransactionsGot==0)) //the condition of getting 0 transactions by
                                     // previous main node should be added!!!!!!!!!!!!!!!!!!!!!
  {
    node_->becomeWriter();
#ifdef MYLOG
    std::cout << "Solver -> Node Level changed 2 -> 3" << std::endl;
#endif
#ifdef SPAM_MAIN
    createSpam = false;
    spamThread.join();
    prepareBlockForSend(testPool);
    node_->sendBlock(testPool);
#else
    prepareBlockForSend(m_pool);

    b_pool.set_sequence((node_->getBlockChain().getLastWrittenSequence()) + 1);
    csdb::PoolHash prev_hash;
    prev_hash.from_string("");
    b_pool.set_previous_hash(prev_hash);

    std::cout << "Solver -> new sequence: " << m_pool.sequence()
              << ", new time:" << m_pool.user_field(0).value<std::string>().c_str() << std::endl;

    node_->sendBlock(std::move(m_pool));
    node_->sendBadBlock(std::move(b_pool));
    std::cout << "Solver -> Block is sent ... awaiting hashes" << std::endl;
#endif
    node_->getBlockChain().setGlobalSequence(m_pool.sequence());
#ifdef MYLOG
    std::cout << "Solver -> Global Sequence: " << node_->getBlockChain().getGlobalSequence() << std::endl;
    std::cout << "Solver -> Writing New Block" << std::endl;
#endif
    node_->getBlockChain().putBlock(m_pool);
  }
}

bool Solver::mPoolClosed() {
  return m_pool_closed;
}

void Solver::runMainRound() {
  m_pool_closed = false;
  std::cout << "========================================================================================" << std::endl;
  std::cout << "VVVVVVVVVVVVVVVVVVVVVVVVV -= TRANSACTION RECEIVING IS ON =- VVVVVVVVVVVVVVVVVVVVVVVVVVVV" << std::endl;

  if (node_->getRoundNumber() == 1) {
    runAfter(std::chrono::milliseconds(2000), [this]() { closeMainRound(); });
  } else {
    runAfter(std::chrono::milliseconds(TIME_TO_COLLECT_TRXNS), [this]() { closeMainRound(); });
  }
}

HashVector Solver::getMyVector() const {
  return hvector;
}

HashMatrix Solver::getMyMatrix() const {
  return (generals->getMatrix());
}

void Solver::flushTransactions() {
  if (node_->getMyLevel() != NodeLevel::Normal)
    return;

  cs::Lock lock(mSharedMutex);

  for (auto& packet : mTransactionsBlock) {
    auto trxCount = packet.transactions_count();
    cslog() << "Transactions in packet: " << trxCount;

    if (trxCount != 0 && packet.hash().is_empty()) {
      // SUPER KOSTIL
      packet.set_storage(node_->getBlockChain().getStorage());

      bool composed = packet.compose();

      if (!composed)
        cslog() << "Transaction compose failed";

      node_->sendTransactionsPacket(packet);
      sentTransLastRound = true;

      csdebug() << "FlushTransaction ...";
      cslog() << "Transaction flushed";

      if (composed) {
        auto hash = packet.hash();

        if (!mHashTable.count(hash))
          mHashTable.insert(std::make_pair(std::move(hash), std::move(packet)));
      }
    }
  }
  mTransactionsBlock.clear();
}

bool Solver::getIPoolClosed() {
  return m_pool_closed;
}

void Solver::gotTransaction(csdb::Transaction&& transaction) {
  if (m_pool_closed) {
#ifdef MYLOG
    LOG_EVENT("m_pool_closed already, cannot accept your transactions");
#endif
    return;
  }

  if (transaction.is_valid()) {
#ifndef SPAMMER
    auto     v       = transaction.to_byte_stream_for_sig();
    size_t   msg_len = v.size();
    uint8_t* message = new uint8_t[msg_len];
    for (size_t i = 0; i < msg_len; i++)
      message[i] = v[i];

    auto    vec = transaction.source().public_key();
    uint8_t public_key[32];
    for (int i = 0; i < 32; i++)
      public_key[i] = vec[i];

    std::string sig_str = transaction.signature();
    uint8_t*    signature;
    signature = (uint8_t*)sig_str.c_str();

    if (verify_signature(signature, public_key, message, msg_len)) {
#endif
      v_pool.add_transaction(transaction);
#ifndef SPAMMER
    } else {
      LOG_EVENT("Wrong signature");
    }
    delete[] message;
#endif
  } else {
#ifdef MYLOG
    LOG_EVENT("Invalid transaction received");
#endif
  }
}

void Solver::gotTransactionsPacket(cs::TransactionsPacket&& packet) {
  csdebug() << "Got transaction packet";

  cs::TransactionsPacketHash hash = packet.hash();
  cs::Lock                   lock(mSharedMutex);

  if (!mHashTable.count(hash))
    mHashTable.insert(std::make_pair(std::move(hash), std::move(packet)));
}

void Solver::gotPacketHashesRequest(std::vector<cs::TransactionsPacketHash>&& hashes, const PublicKey& sender) {
  csdebug() << "Got transactions hash request, try to find in hash table";

  cs::SharedLock lock(mSharedMutex);

  for (const auto& hash : hashes) {
    if (mHashTable.count(hash))
      node_->sendPacketHashesReply(mHashTable[hash], sender);
    // void Node::sendPacketHashesReply(const cs::TransactionsPacket& packet, const PublicKey& sender)
    csdebug() << "Found hash in hash table, send to requester";
  }
}

void Solver::initConfRound() {
  // memset(receivedVecFrom, 0, 100);
  // memset(receivedMatFrom, 0, 100);
  // trustedCounterVector = 0;
  // trustedCounterMatrix = 0;
  // size_t _rNum = rNum;
  // if (gotBigBang) sendZeroVector();

  // runAfter(std::chrono::milliseconds(TIME_TO_AWAIT_ACTIVITY),
  //  [this, _rNum]() { if(!transactionListReceived) node_->sendTLRequest(_rNum); });
}

void Solver::gotPacketHashesReply(cs::TransactionsPacket&& packet) {
  csdebug() << "Got packet hash reply";
  cslog() << "Got packet hash reply";
  cs::TransactionsPacketHash hash = packet.hash();
  cs::Lock                   lock(mSharedMutex);

  if (!mHashTable.count(hash)) {
    mHashTable.insert(std::make_pair(hash, std::move(packet)));
  }

  auto it = std::find(mNeededHashes.begin(), mNeededHashes.end(), hash);

  if (it != mNeededHashes.end()) {
    mNeededHashes.erase(it);
  }

  if (mNeededHashes.empty()) {
    buildTransactionList();
  }
}

void Solver::gotRound(cs::RoundInfo&& round) {
  // if (mRound.round < round.round)
  //{
  cslog() << "Got round table";

  {
    cs::Lock lock(mSharedMutex);
    mRound = std::move(round);
  }

  cs::Hashes     neededHashes;
  cs::SharedLock lock(mSharedMutex);

  for (const auto& hash : mRound.hashes) {
    if (!mHashTable.count(hash))
      neededHashes.push_back(hash);
  }

  if (!neededHashes.empty()) {
    node_->sendPacketHashesRequest(neededHashes);
  } else {
    if (node_->getMyLevel() == NodeLevel::Confidant) {
      buildTransactionList();
    }
  }

  mNeededHashes = std::move(neededHashes);
  // }
}

void Solver::buildTransactionList() {
  cslog() << "BuildTransactionlist";
  csdb::Pool _pool = csdb::Pool{};

  for (const auto& hash : mRound.hashes) {
    auto        it           = mHashTable.find(hash);
    const auto& transactions = it->second.transactions();
    if (it == mHashTable.end()) {
      cserror() << "HASH NOT FOUND";
      return;
    }
    for (const auto& transaction : transactions) {
      _pool.add_transaction(transaction);
    }
  }


  Hash_ result                              = generals->buildvector(_pool, m_pool);
  receivedVecFrom[node_->getMyConfNumber()] = true;
  hvector.Sender                            = node_->getMyConfNumber();
  hvector.hash                              = result;
  receivedVecFrom[node_->getMyConfNumber()] = true;
  generals->addvector(hvector);
  node_->sendVector(std::move(hvector));
  trustedCounterVector++;
  if (trustedCounterVector == mRound.confidants.size()) {
    vectorComplete = true;
    memset(receivedVecFrom, 0, 100);
    trustedCounterVector = 0;
    // compose and send matrix!!!
    // receivedMat_ips.insert(node_->getMyId());
    generals->addSenderToMatrix(node_->getMyConfNumber());
    receivedMatFrom[node_->getMyConfNumber()] = true;
    ++trustedCounterMatrix;
    node_->sendMatrix(generals->getMatrix());
    generals->addmatrix(generals->getMatrix(), node_->getConfidants());  // MATRIX SHOULD BE DECOMPOSED HERE!!!
#ifdef MYLOG
    std::cout << "SOLVER> Matrix added" << std::endl;
#endif
  }
}

void Solver::sendZeroVector() {
  if (transactionListReceived && !getBigBangStatus()) {
    return;
  }
  csdb::Pool test_pool = csdb::Pool{};
}

void Solver::gotVector(HashVector&& vector) {
  std::cout << "SOLVER> GotVector" << std::endl;
  if (receivedVecFrom[vector.Sender] == true) {
    std::cout << "SOLVER> I've already got the vector from this Node" << std::endl;
    return;
  }
  const std::vector<PublicKey>& confidants = node_->getConfidants();
  uint8_t                       numGen     = confidants.size();
  receivedVecFrom[vector.Sender]           = true;

  generals->addvector(vector);  // building matrix
  trustedCounterVector++;

  if (trustedCounterVector == numGen) {
    vectorComplete = true;
    memset(receivedVecFrom, 0, 100);
    trustedCounterVector = 0;
    // compose and send matrix!!!
    uint8_t confNumber = node_->getMyConfNumber();
    generals->addSenderToMatrix(confNumber);
    receivedMatFrom[confNumber] = true;
    trustedCounterMatrix++;

    HashMatrix matrix = generals->getMatrix();
    node_->sendMatrix(matrix);
    generals->addmatrix(matrix, confidants);  // MATRIX SHOULD BE DECOMPOSED HERE!!!

    if (trustedCounterMatrix == numGen) {
      memset(receivedMatFrom, 0, 100);
      uint8_t wTrusted     = (generals->take_decision(
          mRound.confidants, node_->getBlockChain().getHashBySequence(node_->getRoundNumber() - 1)));
      trustedCounterMatrix = 0;

      if (wTrusted == 100) {
        std::cout << "SOLVER> CONSENSUS WASN'T ACHIEVED!!!" << std::endl;
        runAfter(std::chrono::milliseconds(TIME_TO_COLLECT_TRXNS), [this]() { writeNewBlock(); });
      } else {
        std::cout << "SOLVER> wTrusted = " << (int)wTrusted << std::endl;
        consensusAchieved = true;
        if (wTrusted == node_->getMyConfNumber()) {
          node_->becomeWriter();
          runAfter(std::chrono::milliseconds(TIME_TO_COLLECT_TRXNS), [this]() {
            csdb::Pool emptyMetaPool;
            addTimestampToPool(emptyMetaPool);
            emptyMetaPool.set_sequence((node_->getBlockChain().getLastWrittenSequence()) + 1);

            const Characteristic&       characteristic = generals->getCharacteristic();
            uint32_t                    bitsCount      = characteristic.size;
            const std::vector<uint8_t>& mask           = characteristic.mask;

            node_->sendCharacteristic(emptyMetaPool, bitsCount, mask);
            writeNewBlock();
          });
        }
      }
    }
  }
  std::cout << "Solver>  VECTOR GOT SUCCESSFULLY!!!" << std::endl;
}

void Solver::checkMatrixReceived() {
  if (trustedCounterMatrix < 2)
    node_->sendMatrix(generals->getMatrix());
}

void Solver::setRNum(size_t _rNum) {
  rNum = _rNum;
}

void Solver::checkVectorsReceived(size_t _rNum) {
  if (_rNum < rNum)
    return;
  uint8_t numGen = node_->getConfidants().size();
  if (trustedCounterVector == numGen)
    return;
}

void Solver::gotMatrix(HashMatrix&& matrix) {
  uint8_t numGen = node_->getConfidants().size();

  if (gotBlockThisRound) {
    return;
  }
  if (receivedMatFrom[matrix.Sender]) {
    std::cout << "SOLVER> I've already got the matrix from this Node" << std::endl;
    return;
  }

  receivedMatFrom[matrix.Sender] = true;
  trustedCounterMatrix++;
  generals->addmatrix(matrix, node_->getConfidants());

  if (trustedCounterMatrix == numGen) {
    memset(receivedMatFrom, 0, 100);
    uint8_t wTrusted     = (generals->take_decision(mRound.confidants,
                                                node_->getBlockChain().getHashBySequence(node_->getRoundNumber() - 1)));
    trustedCounterMatrix = 0;

    if (wTrusted == 100) {
      std::cout << "SOLVER> CONSENSUS WASN'T ACHIEVED!!!" << std::endl;
      runAfter(std::chrono::milliseconds(TIME_TO_COLLECT_TRXNS), [this]() { writeNewBlock(); });
    } else {
      std::cout << "SOLVER> wTrusted = " << (int)wTrusted << std::endl;
      consensusAchieved = true;
      if (wTrusted == node_->getMyConfNumber()) {
        node_->becomeWriter();
        runAfter(std::chrono::milliseconds(TIME_TO_COLLECT_TRXNS), [this]() {
          csdb::Pool emptyMetaPool;
          addTimestampToPool(emptyMetaPool);
          emptyMetaPool.set_sequence((node_->getBlockChain().getLastWrittenSequence()) + 1);

          const Characteristic&       characteristic = generals->getCharacteristic();
          uint32_t                    bitsCount      = characteristic.size;
          const std::vector<uint8_t>& mask           = characteristic.mask;

          node_->sendCharacteristic(emptyMetaPool, bitsCount, mask);
          writeNewBlock();
        });
      }
    }
  }
}

// what block does this function write???
void Solver::writeNewBlock() {
#ifdef MYLOG
  std::cout << "Solver -> writeNewBlock ... start";
#endif
  if (consensusAchieved && node_->getMyLevel() == NodeLevel::Writer) {
    prepareBlockForSend(m_pool);

    node_->sendBlock(std::move(m_pool));
    node_->getBlockChain().putBlock(m_pool);
    node_->getBlockChain().setGlobalSequence(m_pool.sequence());
    b_pool.set_sequence((node_->getBlockChain().getLastWrittenSequence()) + 1);
    csdb::PoolHash prev_hash;
    prev_hash.from_string("");
    b_pool.set_previous_hash(prev_hash);
    consensusAchieved = false;
  }
}

bool Solver::getBigBangStatus() {
  return gotBigBang;
}

void Solver::setBigBangStatus(bool _status) {
  gotBigBang = _status;
}

void Solver::gotBadBlockHandler(csdb::Pool&& _pool, const PublicKey& sender) {
  // insert code here
}

void Solver::gotBlockCandidate(csdb::Pool&& block) {
#ifdef MYLOG
  std::cout << "Solver -> getBlockCanditate" << std::endl;
#endif
  if (blockCandidateArrived)
    return;

  // m_pool = std::move(block);

  blockCandidateArrived = true;
  // writeNewBlock();
}

void Solver::gotHash(Hash& hash, const PublicKey& sender) {
  if (round_table_sent) {
    return;
  }
  Hash myHash((char*)(node_->getBlockChain().getLastWrittenHash().to_binary().data()));
#ifdef MYLOG
  std::cout << "Solver -> My Hash: " << byteStreamToHex(myHash.str, 32) << std::endl;
#endif
  if (ips.size() <= min_nodes) {
    if (hash == myHash) {
#ifdef MYLOG
      std::cout << "Solver -> Hashes are good" << std::endl;
#endif
      // hashes.push_back(hash);
      ips.push_back(sender);
    } else {
#ifdef MYLOG
      if (hash != myHash)
        std::cout << "Hashes do not match!!!" << std::endl;
#endif
      return;
    }
  } else {
#ifdef MYLOG
    std::cout << "Solver -> We have enough hashes!" << std::endl;
#endif
    return;
  }
  if ((ips.size() == min_nodes) && (!round_table_sent)) {
#ifdef MYLOG
    std::cout << "Solver -> sending NEW ROUND table" << std::endl;
#endif

    cs::Hashes hashes;
    for (auto& it : mHashTable)
      hashes.push_back(it.first);

    mRound.round      = ++rNum;
    mRound.confidants = std::move(ips);
    mRound.general    = std::move(node_->getMyPublicKey());
    mRound.hashes     = std::move(hashes);
    std::cout << "Solver -> NEW ROUND initialization done" << std::endl;
    node_->initNextRound(mRound);
    round_table_sent = true;
  }
}

void Solver::initApi() {
  _initApi();
}

void Solver::_initApi() {
  //        csconnector::start(&(node_->getBlockChain()),csconnector::Config{});
  //
  //		csstats::start(&(node_->getBlockChain()));
}

/////////////////////////////

#ifdef SPAM_MAIN
void Solver::createPool() {
  std::string        mp  = "0123456789abcdef";
  const unsigned int cmd = 6;

  struct timeb tt;
  ftime(&tt);
  srand(tt.time * 1000 + tt.millitm);

  testPool = csdb::Pool();

  std::string aStr(64, '0');
  std::string bStr(64, '0');

  uint32_t limit = randFT(5, 15);

  if (randFT(0, 150) == 42) {
    csdb::Transaction smart_trans;
    smart_trans.set_currency(csdb::Currency("CS"));

    smart_trans.set_target(Credits::BlockChain::getAddressFromKey("3SHCtvpLkBWytVSqkuhnNk9z1LyjQJaRTBiTFZFwKkXb"));
    smart_trans.set_source(
        csdb::Address::from_string("0000000000000000000000000000000000000000000000000000000000000001"));

    smart_trans.set_amount(csdb::Amount(1, 0));
    smart_trans.set_balance(csdb::Amount(100, 0));

    api::SmartContract sm;
    sm.address = "3SHCtvpLkBWytVSqkuhnNk9z1LyjQJaRTBiTFZFwKkXb";
    sm.method  = "store_sum";
    sm.params  = {"123", "456"};

    smart_trans.add_user_field(0, serialize(sm));

    testPool.add_transaction(smart_trans);
  }

  csdb::Transaction transaction;
  transaction.set_currency(csdb::Currency("CS"));

  while (createSpam && limit > 0) {
    for (size_t i = 0; i < 64; ++i) {
      aStr[i] = mp[randFT(0, 15)];
      bStr[i] = mp[randFT(0, 15)];
    }

    transaction.set_target(csdb::Address::from_string(aStr));
    transaction.set_source(csdb::Address::from_string(bStr));

    transaction.set_amount(csdb::Amount(randFT(1, 1000), 0));
    transaction.set_balance(csdb::Amount(transaction.balance().integral() + 1, 0));

    testPool.add_transaction(transaction);
    --limit;
  }

  addTimestampToPool(testPool);
}
#endif

#ifdef SPAMMER
void Solver::spamWithTransactions() {
  // if (node_->getMyLevel() != Normal) return;
  std::cout << "STARTING SPAMMER..." << std::endl;
  std::string mp = "1234567890abcdef";

  // std::string cachedBlock;
  // cachedBlock.reserve(64000);
  uint64_t iid = 0;
  std::this_thread::sleep_for(std::chrono::seconds(5));

  auto aaa = csdb::Address::from_string("0000000000000000000000000000000000000000000000000000000000000001");
  auto bbb = csdb::Address::from_string("0000000000000000000000000000000000000000000000000000000000000002");

  csdb::Transaction transaction;
  transaction.set_target(aaa);
  transaction.set_source(csdb::Address::from_public_key((char*)myPublicKey.data()));
  transaction.set_currency(csdb::Currency("CS"));

  while (true) {
    if (spamRunning && (node_->getMyLevel() == Normal)) {
      if ((node_->getRoundNumber() < 10) || (node_->getRoundNumber() > 20)) {
        transaction.set_amount(csdb::Amount(randFT(1, 1000), 0));
        transaction.set_comission(csdb::Amount(0, 1, 10));
        transaction.set_balance(csdb::Amount(transaction.amount().integral() + 2, 0));
        transaction.set_innerID(iid);
        addTransaction(transaction);
        iid++;
      }
    }
    std::this_thread::sleep_for(std::chrono::microseconds(TRX_SLEEP_TIME));
  }
}
#endif

///////////////////

void Solver::send_wallet_transaction(const csdb::Transaction& transaction) {
  cs::Lock lock(mSharedMutex);
  m_transactions.push_back(transaction);
}

void Solver::addInitialBalance() {
  std::cout << "===SETTING DB===" << std::endl;
  const std::string start_address = "0000000000000000000000000000000000000000000000000000000000000002";
  csdb::Pool        pool;
  csdb::Transaction transaction;
  transaction.set_target(csdb::Address::from_public_key((char*)myPublicKey.data()));
  transaction.set_source(csdb::Address::from_string(start_address));

  transaction.set_currency(csdb::Currency("CS"));
  transaction.set_amount(csdb::Amount(10000, 0));
  transaction.set_balance(csdb::Amount(10000000, 0));
  transaction.set_innerID(1);

  {
    cs::Lock lock(mSharedMutex);
    m_transactions.push_back(transaction);
  }

#ifdef SPAMMER
  spamThread = std::thread(&Solver::spamWithTransactions, this);
  spamThread.detach();
#endif
}

cs::RoundNumber Solver::currentRoundNumber() {
  return mRound.round;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///gotBlockRequest
void Solver::gotBlockRequest(csdb::PoolHash&& hash, const PublicKey& nodeId) {
  csdb::Pool pool = node_->getBlockChain().loadBlock(hash);
  if (pool.is_valid()) {
    csdb::PoolHash prev_hash;
    prev_hash.from_string("");
    pool.set_previous_hash(prev_hash);
    node_->sendBlockReply(std::move(pool), nodeId);
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///gotBlockReply
void Solver::gotBlockReply(csdb::Pool&& pool) {
#ifdef MYLOG
  std::cout << "Solver -> Got Block for my Request: " << pool.sequence() << std::endl;
#endif
  if (pool.sequence() == node_->getBlockChain().getLastWrittenSequence() + 1)
    node_->getBlockChain().putBlock(pool);
}

void Solver::addConfirmation(uint8_t confNumber_) {
  if (writingConfGotFrom[confNumber_])
    return;
  writingConfGotFrom[confNumber_] = true;
  writingCongGotCurrent++;
  if (writingCongGotCurrent == 2) {
    node_->becomeWriter();
    runAfter(std::chrono::milliseconds(TIME_TO_COLLECT_TRXNS), [this]() { writeNewBlock(); });
  }
}

void Solver::nextRound() {
#ifdef MYLOG
  std::cout << "SOLVER> next Round : Starting ... nextRound" << std::endl;
#endif
  receivedVec_ips.clear();
  receivedMat_ips.clear();

  hashes.clear();
  ips.clear();
  vector_datas.clear();

  vectorComplete          = false;
  consensusAchieved       = false;
  blockCandidateArrived   = false;
  transactionListReceived = false;
  vectorReceived          = false;
  gotBlockThisRound       = false;

  round_table_sent   = false;
  sentTransLastRound = false;
  m_pool = csdb::Pool{};
  if (m_pool_closed)
    v_pool = csdb::Pool{};
  if (node_->getMyLevel() == NodeLevel::Confidant) {
    memset(receivedVecFrom, 0, 100);
    memset(receivedMatFrom, 0, 100);
    trustedCounterVector = 0;
    trustedCounterMatrix = 0;
    // if (gotBigBang) sendZeroVector();

#ifdef MYLOG
    std::cout << "SOLVER> next Round : the variables initialized" << std::endl;
#endif

    // runMainRound();

#ifdef SPAM_MAIN
    createSpam = true;
    spamThread = std::thread(&Solver::createPool, this);
#endif
#ifdef SPAMMER
    spamRunning = false;
#endif
  } else {
#ifdef SPAMMER
    spamRunning = true;
#endif
    m_pool_closed = true;
    if (!m_SendingPacketTimer.isRunning()) {
      cslog() << "Transaction timer started";
      m_SendingPacketTimer.start(TransactionsPacketInterval);
    }
  }
}

bool Solver::verify_signature(uint8_t signature[64], uint8_t public_key[32], uint8_t* message, size_t message_len) {
  int ver_ok = crypto_sign_ed25519_verify_detached(signature, message, message_len, public_key);
  return (ver_ok == 0);
}

void Solver::addTransaction(const csdb::Transaction& transaction) {
  cs::Lock    lock(mSharedMutex);
  std::size_t packetIndex = 0;

  // TODO: fix algorithm for optimization
  for (auto& packet : mTransactionsBlock) {
    if (packet.transactions_count() >= MaxPacketTransactions)
      ++packetIndex;
    else
      break;
  }

  if (mTransactionsBlock.size() <= packetIndex) {
    mTransactionsBlock.emplace_back(csdb::Pool{});
  }

  mTransactionsBlock[packetIndex].add_transaction(transaction);
}

void Solver::setConfidants(const std::vector<PublicKey>& confidants, const PublicKey& general,
                           const RoundNumber roundNum) {
  cs::Lock lock(mSharedMutex);
  mRound.confidants = confidants;
  mRound.round      = roundNum;
  mRound.hashes.clear();
  mRound.general = general;
}

}  // namespace cs
