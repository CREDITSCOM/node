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
  // std::cout << "SOLVER> Before calback";
  const auto  tp = std::chrono::system_clock::now() + ms;
  std::thread tr([tp, cb]() {
    std::this_thread::sleep_until(tp);
    //  LOG_WARN("Inserting callback");
    CallsQueue::instance().insert(cb);
  });
  // std::cout << "SOLVER> After calback";
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
, generals(std::unique_ptr<Generals>(new Generals())) {
  m_sendingPacketTimer.connect(std::bind(&Solver::flushTransactions, this));
}

Solver::~Solver() {
  m_sendingPacketTimer.disconnect();
  m_sendingPacketTimer.stop();
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
  // std::cout << "SOLVER> Before time stamp";
  // block is build in buildvector
  addTimestampToPool(block);
  // std::cout << "SOLVER> Before write pub key";
  block.set_writer_public_key(myPublicKey);
  // std::cout << "SOLVER> Before write last sequence";
  block.set_sequence((node_->getBlockChain().getLastWrittenSequence()) + 1);
  auto prev_hash = csdb::PoolHash::from_string("");
  block.set_previous_hash(prev_hash);
  // std::cout << "SOLVER> Before private key";
  block.sign(myPrivateKey);
#ifdef MYLOG
  std::cout
      << "last sequence: " << (node_->getBlockChain().getLastWrittenSequence())
      << std::
             endl;  // ", last time:" <<
                    // node_->getBlockChain().loadBlock(node_->getBlockChain().getLastHash()).user_field(0).value<std::string>().c_str()
  std::cout << "prev_hash: " << node_->getBlockChain().getLastHash().to_string() << " <- Not sending!!!";
  std::cout << "new sequence: " << block.sequence() << ", new time:" << block.user_field(0).value<std::string>().c_str()
           ;
#endif
}

void Solver::sendTL() {
  if (gotBigBang) {
    return;
}
  uint32_t tNum = v_pool.transactions_count();

  std::cout << "AAAAAAAAAAAAAAAAAAAAAAAA -= TRANSACTION RECEIVING IS OFF =- AAAAAAAAAAAAAAAAAAAAAAAAAAAA";
#ifdef MYLOG
  std::cout << "                          Total received " << tNum << " transactions";
#endif
  std::cout << "========================================================================================";

  m_isPoolClosed = true;

  std::cout << "Solver -> Sending " << tNum << " transactions ";

  v_pool.set_sequence(node_->getRoundNumber());
  node_->sendTransactionList(v_pool);  // Correct sending, better when to all one time
}

uint32_t Solver::getTLsize() {
  return v_pool.transactions_count();
}

void Solver::setLastRoundTransactionsGot(size_t trNum) {
  lastRoundTransactionsGot = trNum;
}

void Solver::applyCharacteristic(const std::vector<uint8_t>& characteristic, uint32_t bitsCount,
                                 const csdb::Pool& metaInfoPool, const PublicKey& sender)
{
  cslog() << "SOLVER> ApplyCharacteristic";

  if (node_->getMyLevel() == NodeLevel::Writer) {
    return;
  }

  gotBigBang        = false;
  gotBlockThisRound = true;

  uint64_t sequence = metaInfoPool.sequence();
  cslog() << "SOLVER> ApplyCharacteristic : sequence = " << sequence;
  std::string timestamp = metaInfoPool.user_field(0).value<std::string>();

  boost::dynamic_bitset<> mask{characteristic.begin(), characteristic.end()};

  size_t         maskIndex = 0;
  cs::SharedLock sharedLock(mSharedMutex);

  for (const auto& hash : m_roundInfo.hashes) {
    auto it = m_hashTable.find(hash);
    if (it == m_hashTable.end()) {
      cserror() << "HASH NOT FOUND";
      return;
    }
    const auto& transactions = it->second.transactions();
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
    m_hashTable.erase(it);
  }
  m_pool.set_sequence(sequence);
  m_pool.add_user_field(0, timestamp);

  cslog() << "SOLVER> ApplyCharacteristic: pool created";

#ifdef MONITOR_NODE
  addTimestampToPool(m_pool);
#endif

  uint32_t g_seq = m_pool.sequence();
  csdebug() << "GOT NEW BLOCK: global sequence = " << g_seq;

  if (g_seq > node_->getRoundNumber()) {
    return;  // remove this line when the block candidate signing of all trusted will be implemented
  }

  node_->getBlockChain().setGlobalSequence(g_seq);

  if (g_seq == node_->getBlockChain().getLastWrittenSequence() + 1) {
    node_->getBlockChain().putBlock(m_pool);
#ifndef MONITOR_NODE
    if ((node_->getMyLevel() != NodeLevel::Writer) && (node_->getMyLevel() != NodeLevel::Main)) {
      Hash test_hash((char*)(node_->getBlockChain()
                                 .getLastWrittenHash()
                                 .to_binary()
                                 .data()));  // getLastWrittenHash().to_binary().data()));//SENDING
                                             // HASH!!!
      node_->sendHash(test_hash, sender);

      cslog() << "SENDING HASH to writer: " << byteStreamToHex(test_hash.str, 32);
    }
#endif
  }
}

void Solver::closeMainRound() {
  if (node_->getRoundNumber() == 1)  // || (lastRoundTransactionsGot==0)) //the condition of getting 0 transactions by
                                     // previous main node should be added!!!!!!!!!!!!!!!!!!!!!
  {
    node_->becomeWriter();
    csdebug() << "Solver -> Node Level changed 2 -> 3";

#ifdef SPAM_MAIN
    createSpam = false;
    spamThread.join();
    prepareBlockForSend(testPool);
    node_->sendBlock(testPool);
#else
    prepareBlockForSend(m_pool);

    b_pool.set_sequence((node_->getBlockChain().getLastWrittenSequence()) + 1);
    auto prev_hash = csdb::PoolHash::from_string("");
    b_pool.set_previous_hash(prev_hash);

    std::cout << "Solver -> new sequence: " << m_pool.sequence()
              << ", new time:" << m_pool.user_field(0).value<std::string>().c_str();

    node_->sendBlock(m_pool);
    node_->sendBadBlock(b_pool);
    std::cout << "Solver -> Block is sent ... awaiting hashes";
#endif
    node_->getBlockChain().setGlobalSequence(m_pool.sequence());
    csdebug() << "Solver -> Global Sequence: " << node_->getBlockChain().getGlobalSequence();
    csdebug() << "Solver -> Writing New Block";
    node_->getBlockChain().putBlock(m_pool);
  }
}

bool Solver::isPoolClosed() {
  return m_isPoolClosed;
}

void Solver::runMainRound() {
  m_isPoolClosed = false;
  std::cout << "========================================================================================";
  std::cout << "VVVVVVVVVVVVVVVVVVVVVVVVV -= TRANSACTION RECEIVING IS ON =- VVVVVVVVVVVVVVVVVVVVVVVVVVVV";

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
  if (node_->getMyLevel() != NodeLevel::Normal) {
    return;
  }

  cs::Lock lock(mSharedMutex);

  for (auto& packet : m_transactionsBlock) {
    auto trxCount = packet.transactions_count();
    cslog() << "Transactions in packet: " << trxCount;

    if (trxCount != 0 && packet.hash().is_empty()) {
      // SUPER KOSTIL
      packet.set_storage(node_->getBlockChain().getStorage());

      bool composed = packet.compose();
      node_->sendTransactionsPacket(packet);
      sentTransLastRound = true;

      csdebug() << "FlushTransaction ...";
      cslog() << "Transaction flushed";

      if (composed && !m_hashTable.count(packet.hash())) {
<<<<<<< HEAD
        m_hashTable.insert(std::make_pair(packet.hash(), std::move(packet)));
=======
        m_hashTable.insert(std::make_pair(std::move(hash), std::move(packet)));
>>>>>>> c1fcb3709753362df66f2304e7e13eb102ea48c1
      } else {
        cslog() << "Transaction compose failed";
      }
    }
  }
  m_transactionsBlock.clear();
}

bool Solver::getIPoolClosed() {
  return m_isPoolClosed;
}

void Solver::gotTransaction(csdb::Transaction&& transaction) { // reviewer: "Need to refactoring!"
  if (m_isPoolClosed) {
    csdebug() << "m_isPoolClosed already, cannot accept your transactions";
    return;
  }

  if (transaction.is_valid()) {
#ifndef SPAMMER
    auto   v       = transaction.to_byte_stream_for_sig();
    size_t msg_len = v.size();
    auto   message = new uint8_t[msg_len];
    for (size_t i = 0; i < msg_len; i++) {
      message[i] = v[i];
    }
    auto    vec = transaction.source().public_key();
    uint8_t public_key[32];
    for (int i = 0; i < 32; i++) {
      public_key[i] = vec[i];
    }
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
    csdebug() << "Invalid transaction received";
  }
}

void Solver::gotTransactionsPacket(cs::TransactionsPacket&& packet) {
  csdebug() << "Got transaction packet";
  cs::TransactionsPacketHash hash = packet.hash();

  cs::Lock lock(mSharedMutex);
  if (!m_hashTable.count(hash))
    m_hashTable.insert(std::make_pair(std::move(hash), std::move(packet)));
}

void Solver::gotPacketHashesRequest(std::vector<cs::TransactionsPacketHash>&& hashes, const PublicKey& sender) {
  csdebug() << "Got transactions hash request, try to find in hash table";
  cs::SharedLock lock(mSharedMutex);
  for (const auto& hash : hashes) {
    if (m_hashTable.count(hash)) {
      node_->sendPacketHashesReply(m_hashTable[hash], sender);
    }
    csdebug() << "Found hash in hash table, send to requester";
  }
}

void Solver::initConfRound() {
}

void Solver::gotPacketHashesReply(cs::TransactionsPacket&& packet) {
  csdebug() << "Got packet hash reply";
  cslog() << "Got packet hash reply";
  cs::TransactionsPacketHash hash = packet.hash();
  cs::Lock                   lock(mSharedMutex);

  if (!m_hashTable.count(hash)) {
    m_hashTable.insert(std::make_pair(hash, std::move(packet)));
  }

  auto it = std::find(m_neededHashes.begin(), m_neededHashes.end(), hash);

  if (it != m_neededHashes.end()) {
    m_neededHashes.erase(it);
  }

  if (m_neededHashes.empty()) {
    buildTransactionList();
  }
}

void Solver::gotRound(cs::RoundInfo&& round) {
  cslog() << "Got round table";
  {
    cs::Lock lock(mSharedMutex);
    m_roundInfo = std::move(round);
  }
  cs::Hashes     neededHashes;
  cs::SharedLock lock(mSharedMutex);

  for (const auto& hash : m_roundInfo.hashes) {
    if (!m_hashTable.count(hash)) {
      neededHashes.push_back(hash);
    }
  }
  if (!neededHashes.empty()) {
    node_->sendPacketHashesRequest(neededHashes);
  } else if (node_->getMyLevel() == NodeLevel::Confidant) {
    buildTransactionList();
  }
  m_neededHashes = std::move(neededHashes);
}

void Solver::buildTransactionList() {
  cslog() << "BuildTransactionlist";
  csdb::Pool _pool = csdb::Pool{};

  for (const auto& hash : m_roundInfo.hashes) {
    auto it = m_hashTable.find(hash);
    if (it == m_hashTable.end()) {
      cserror() << "HASH NOT FOUND";
      return;
    }
    const auto& transactions = it->second.transactions();
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
  node_->sendVector(hvector);
  trustedCounterVector++;
  if (trustedCounterVector == m_roundInfo.confidants.size()) {
    vectorComplete = true;
    memset(receivedVecFrom, 0, 100);
    trustedCounterVector = 0;
    // compose and send matrix!!!
    generals->addSenderToMatrix(node_->getMyConfNumber());
    receivedMatFrom[node_->getMyConfNumber()] = true;
    ++trustedCounterMatrix;
    node_->sendMatrix(generals->getMatrix());
    generals->addmatrix(generals->getMatrix(), node_->getConfidants());  // MATRIX SHOULD BE DECOMPOSED HERE!!!
    csdebug() << "SOLVER> Matrix added";
  }
}

void Solver::sendZeroVector() {
  if (transactionListReceived && !getBigBangStatus()) {
    return;
  }
}

void Solver::gotVector(HashVector&& vector) {
  std::cout << "SOLVER> GotVector";
  if (receivedVecFrom[vector.Sender] == true) {
    std::cout << "SOLVER> I've already got the vector from this Node";
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
          m_roundInfo.confidants, node_->getBlockChain().getHashBySequence(node_->getRoundNumber() - 1)));
      trustedCounterMatrix = 0;

      if (wTrusted == 100) {
        std::cout << "SOLVER> CONSENSUS WASN'T ACHIEVED!!!";
        runAfter(std::chrono::milliseconds(TIME_TO_COLLECT_TRXNS), [this]() { writeNewBlock(); });
      } else {
        std::cout << "SOLVER> wTrusted = " << (int)wTrusted;
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
  std::cout << "Solver>  VECTOR GOT SUCCESSFULLY!!!";
}

void Solver::checkMatrixReceived() {
  if (trustedCounterMatrix < 2) {
    node_->sendMatrix(generals->getMatrix());
  }
}

void Solver::setRNum(size_t _rNum) {
  rNum = _rNum;
}

void Solver::checkVectorsReceived(size_t _rNum) {
  if (_rNum < rNum) {
    return;
  }
  const uint8_t numGen = node_->getConfidants().size();
  if (trustedCounterVector == numGen) {
    return;
  }
}

void Solver::gotMatrix(HashMatrix&& matrix) {
  if (gotBlockThisRound) {
    return;
  }
  if (receivedMatFrom[matrix.Sender]) {
    std::cout << "SOLVER> I've already got the matrix from this Node";
    return;
  }
  receivedMatFrom[matrix.Sender] = true;
  trustedCounterMatrix++;
  generals->addmatrix(matrix, node_->getConfidants());

  const uint8_t numGen = node_->getConfidants().size();
  if (trustedCounterMatrix == numGen) {
    memset(receivedMatFrom, 0, 100);
    uint8_t wTrusted     = (generals->take_decision(m_roundInfo.confidants,
                                                node_->getBlockChain().getHashBySequence(node_->getRoundNumber() - 1)));
    trustedCounterMatrix = 0;

    if (wTrusted == 100) {
      std::cout << "SOLVER> CONSENSUS WASN'T ACHIEVED!!!";
      runAfter(std::chrono::milliseconds(TIME_TO_COLLECT_TRXNS), [this]() { writeNewBlock(); });
    } else {
      std::cout << "SOLVER> wTrusted = " << (int)wTrusted;
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
  csdebug() << "Solver -> writeNewBlock ... start";
  if (consensusAchieved && node_->getMyLevel() == NodeLevel::Writer) {
    prepareBlockForSend(m_pool);
    node_->sendBlock(m_pool);
    node_->getBlockChain().putBlock(m_pool);
    node_->getBlockChain().setGlobalSequence(m_pool.sequence());
    b_pool.set_sequence((node_->getBlockChain().getLastWrittenSequence()) + 1);
    auto prev_hash = csdb::PoolHash::from_string("");
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
  csdebug() << "Solver -> getBlockCanditate";
  if (blockCandidateArrived) {
    return;
  }
  blockCandidateArrived = true;
}

void Solver::gotHash(Hash& hash, const PublicKey& sender) {
  if (round_table_sent) {
    return;
  }
  Hash myHash((char*)(node_->getBlockChain().getLastWrittenHash().to_binary().data()));
  csdebug() << "Solver -> My Hash: " << byteStreamToHex(myHash.str, 32);
  if (ips.size() <= min_nodes) {
    if (hash == myHash) {
      csdebug() << "Solver -> Hashes are good";
      ips.push_back(sender);
    } else {
      cslog() << "Hashes do not match!!!";
      return;
    }
  } else {
    cslog() << "Solver -> We have enough hashes!";
    return;
  }
  if ((ips.size() == min_nodes) && (!round_table_sent)) {
    cslog() << "Solver -> sending NEW ROUND table";
    cs::Hashes hashes;
    for (auto& it : m_hashTable) {
      hashes.push_back(it.first);
    }

    m_roundInfo.round      = ++rNum;
    m_roundInfo.confidants = std::move(ips);
    m_roundInfo.general    = node_->getMyPublicKey();
    m_roundInfo.hashes     = std::move(hashes);
    std::cout << "Solver -> NEW ROUND initialization done";
    node_->initNextRound(m_roundInfo);
    round_table_sent = true;
  }
}

void Solver::initApi() {
  _initApi();
}

void Solver::_initApi() {
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
  std::cout << "STARTING SPAMMER...";
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
  std::cout << "===SETTING DB===";
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
  return m_roundInfo.round;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// gotBlockRequest
void Solver::gotBlockRequest(csdb::PoolHash&& hash, const PublicKey& nodeId) {
  csdb::Pool pool = node_->getBlockChain().loadBlock(hash);
  if (pool.is_valid()) {
    auto prev_hash = csdb::PoolHash::from_string("");
    pool.set_previous_hash(prev_hash);
    node_->sendBlockReply(pool, nodeId);
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// gotBlockReply
void Solver::gotBlockReply(csdb::Pool&& pool) {
  cslog() << "Solver -> Got Block for my Request: " << pool.sequence() ;
  if (pool.sequence() == node_->getBlockChain().getLastWrittenSequence() + 1)
    node_->getBlockChain().putBlock(pool);
}

void Solver::addConfirmation(uint8_t confNumber_) {
  if (writingConfGotFrom[confNumber_]) {
    return;
  }
  writingConfGotFrom[confNumber_] = true;
  writingCongGotCurrent++;
  if (writingCongGotCurrent == 2) {
    node_->becomeWriter();
    runAfter(std::chrono::milliseconds(TIME_TO_COLLECT_TRXNS), [this]() { writeNewBlock(); });
  }
}

void Solver::nextRound() {
  cslog() << "SOLVER> next Round : Starting ... nextRound";

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
  round_table_sent        = false;
  sentTransLastRound      = false;
  m_pool                  = csdb::Pool{};

  if (m_isPoolClosed) {
    v_pool = csdb::Pool{};
  }
  if (node_->getMyLevel() == NodeLevel::Confidant) {
    memset(receivedVecFrom, 0, 100);
    memset(receivedMatFrom, 0, 100);
    trustedCounterVector = 0;
    trustedCounterMatrix = 0;
    cslog() << "SOLVER> next Round : the variables initialized";

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
    m_isPoolClosed = true;
    if (!m_sendingPacketTimer.isRunning()) {
      cslog() << "Transaction timer started";
      m_sendingPacketTimer.start(TransactionsPacketInterval);
    }
  }
}

bool Solver::verify_signature(uint8_t signature[64], uint8_t public_key[32], uint8_t* message, size_t message_len) {
  int ver_ok = crypto_sign_ed25519_verify_detached(signature, message, message_len, public_key);
  return ver_ok == 0;
}

void Solver::addTransaction(const csdb::Transaction& transaction) {
  cs::Lock    lock(mSharedMutex);
  std::size_t packetIndex = 0;

  // TODO: fix algorithm for optimization
  for (auto& packet : m_transactionsBlock) {
    if (packet.transactions_count() >= MaxPacketTransactions) {
      ++packetIndex;
    } else {
      break;
    }
  }
  if (m_transactionsBlock.size() <= packetIndex) {
    m_transactionsBlock.emplace_back(csdb::Pool{});
  }
  m_transactionsBlock[packetIndex].add_transaction(transaction);
}

void Solver::setConfidants(const std::vector<PublicKey>& confidants, const PublicKey& general,
                           const RoundNumber roundNum) {
  cs::Lock lock(mSharedMutex);
  m_roundInfo.confidants = confidants;
  m_roundInfo.round      = roundNum;
  m_roundInfo.hashes.clear();
  m_roundInfo.general = general;
}

}  // namespace cs
