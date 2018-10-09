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
#include "solver/generals.hpp"
#include "solver/solver.hpp"

#include <lib/system/logger.hpp>
#include <lib/system/utils.hpp>

#include <base58.h>
#include <sodium.h>

namespace {
void addTimestampToPool(csdb::Pool& pool) {
  auto now_time = std::chrono::system_clock::now();
  pool.add_user_field(
      0, std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(now_time.time_since_epoch()).count()));
}

#if defined(SPAM_MAIN) || defined(SPAMMER)
static int randFT(int min, int max) {
  return rand() % (max - min + 1) + min;
}
#endif
}  // namespace

namespace cs {
constexpr short min_nodes = 3;

Solver::Solver(Node* node)
: m_node(node)
, m_generals(std::unique_ptr<Generals>(new Generals()))
, m_writerIndex(0) {
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
  addTimestampToPool(block);
  block.set_writer_public_key(myPublicKey);
  block.set_sequence((m_node->getBlockChain().getLastWrittenSequence()) + 1);

  auto prev_hash = csdb::PoolHash::from_string("");
  block.set_previous_hash(prev_hash);
  block.sign(myPrivateKey);

  cslog() << "last sequence: " << (m_node->getBlockChain().getLastWrittenSequence());
  cslog() << "prev_hash: " << m_node->getBlockChain().getLastHash().to_string() << " <- Not sending!!!";
  cslog() << "new sequence: " << block.sequence() << ", new time:" << block.user_field(0).value<std::string>().c_str();
}

void Solver::sendTL() {
  if (gotBigBang) {
    return;
  }

  uint32_t tNum = static_cast<uint32_t>(v_pool.transactions_count());

  cslog() << "AAAAAAAAAAAAAAAAAAAAAAAA -= TRANSACTION RECEIVING IS OFF =- AAAAAAAAAAAAAAAAAAAAAAAAAAAA";
  csdebug() << "                          Total received " << tNum << " transactions";
  cslog() << "========================================================================================";

  m_isPoolClosed = true;

  cslog() << "Solver -> Sending " << tNum << " transactions ";

  v_pool.set_sequence(m_node->getRoundNumber());
  m_node->sendTransactionList(v_pool);  // Correct sending, better when to all one time
}

uint32_t Solver::getTLsize() {
  return static_cast<uint32_t>(v_pool.transactions_count());
}

auto Solver::setLastRoundTransactionsGot(size_t trNum) -> void {
  lastRoundTransactionsGot = trNum;
}

void Solver::applyCharacteristic(const std::vector<uint8_t>& characteristic, uint32_t bitsCount,
                                 const PoolMetaInfo& metaInfoPool, const PublicKey& sender) {
  cslog() << "SOLVER> ApplyCharacteristic";

  if (m_node->getMyLevel() == NodeLevel::Writer) {
    return;
  }

  gotBigBang        = false;
  gotBlockThisRound = true;

  uint64_t sequence = metaInfoPool.sequenceNumber;

  cslog() << "SOLVER> ApplyCharacteristic : sequence = " << sequence;

  std::string             timestamp = metaInfoPool.timestamp;
  boost::dynamic_bitset<> mask{characteristic.begin(), characteristic.end()};

  size_t     maskIndex = 0;
  cs::Hashes localHashes;

  {
    cs::SharedLock sharedLock(m_sharedMutex);
    localHashes = m_roundTable.hashes;
  }

  for (const auto& hash : localHashes) {
    if (!m_hashTable.contains(hash)) {
      cserror() << "HASH NOT FOUND";
      return;
    }

    const auto& transactions = m_hashTable.find(hash).transactions();

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


    m_hashTable.erase(hash);
  }

  {
    cs::Lock lock(m_sharedMutex);

    m_pool.set_sequence(sequence);
    m_pool.add_user_field(0, timestamp);
  }
  cslog() << "SOLVER> ApplyCharacteristic: pool created";

#ifdef MONITOR_NODE
  addTimestampToPool(m_pool);
#endif

  csdebug() << "GOT NEW BLOCK: global sequence = " << sequence;

  if (sequence > m_node->getRoundNumber()) {
    return;  // remove this line when the block candidate signing of all trusted will be implemented
  }

  m_node->getBlockChain().setGlobalSequence(static_cast<uint32_t>(sequence));

  // TODO: need to write notifications to csdb::Pool
  if (sequence == (m_node->getBlockChain().getLastWrittenSequence() + 1)) {
    m_node->getBlockChain().putBlock(m_pool);

#ifndef MONITOR_NODE
    if ((m_node->getMyLevel() != NodeLevel::Writer) && (m_node->getMyLevel() != NodeLevel::Main)) {
      auto hash = m_node->getBlockChain().getLastWrittenHash().to_string();

      m_node->sendHash(hash, sender);

      cslog() << "SENDING HASH to writer: " << hash;
    }
#endif
  }
}

const Characteristic& Solver::getCharacteristic() const {
  return m_generals->getCharacteristic();
}

Hash Solver::getCharacteristicHash() const {
  const Characteristic& characteristic = m_generals->getCharacteristic();
  return getBlake2Hash(characteristic.mask.data(), characteristic.mask.size());
}

std::vector<uint8_t> Solver::sign(std::vector<uint8_t> data) {
  std::vector<uint8_t> signature(64);  // 64 is signature length. We need place this as constant!
  unsigned long long   signLength = 0;

  crypto_sign_detached(signature.data(), &signLength, data.data(), data.size(), myPrivateKey.data());

  assert(64 == signLength);  // signature length = 64. Where's constant?

  data.insert(data.end(), signature.begin(), signature.end());

  return data;
}

PublicKey Solver::getWriterPublicKey() const {
  PublicKey result;
  if (m_writerIndex < m_roundTable.confidants.size()) {
    result = m_roundTable.confidants[m_writerIndex];
  } else {
    cserror() << "WRITER PUBLIC KEY IS NOT EXIST AT CONFIDANTS. LOGIC ERROR!";
  }
  return result;
}

void Solver::closeMainRound() {
  if (m_node->getRoundNumber() == 1)  // || (lastRoundTransactionsGot==0)) //the condition of getting 0 transactions by
                                     // previous main node should be added!!!!!!!!!!!!!!!!!!!!!
  {
    m_node->becomeWriter();
    csdebug() << "Solver -> Node Level changed 2 -> 3";

#ifdef SPAM_MAIN
    createSpam = false;
    spamThread.join();
    prepareBlockForSend(testPool);
    node_->sendBlock(testPool);
#else
    prepareBlockForSend(m_pool);

    b_pool.set_sequence((m_node->getBlockChain().getLastWrittenSequence()) + 1);
    auto prev_hash = csdb::PoolHash::from_string("");
    b_pool.set_previous_hash(prev_hash);

    cslog() << "Solver -> new sequence: " << m_pool.sequence()
            << ", new time:" << m_pool.user_field(0).value<std::string>().c_str();

    m_node->sendBlock(m_pool);
    m_node->sendBadBlock(b_pool);

    cslog() << "Solver -> Block is sent ... awaiting hashes";
#endif
    m_node->getBlockChain().setGlobalSequence(static_cast<uint32_t>(m_pool.sequence()));

    csdebug() << "Solver -> Global Sequence: " << m_node->getBlockChain().getGlobalSequence();
    csdebug() << "Solver -> Writing New Block";

    m_node->getBlockChain().putBlock(m_pool);
  }
}

bool Solver::isPoolClosed() const {
  return m_isPoolClosed;
}

void Solver::runMainRound() {
  m_isPoolClosed = false;

  cslog() << "========================================================================================";
  cslog() << "VVVVVVVVVVVVVVVVVVVVVVVVV -= TRANSACTION RECEIVING IS ON =- VVVVVVVVVVVVVVVVVVVVVVVVVVVV";

  if (m_node->getRoundNumber() == 1) {
    cs::Utils::runAfter(std::chrono::milliseconds(2000), [this]() { closeMainRound(); });
  } else {
    cs::Utils::runAfter(std::chrono::milliseconds(TIME_TO_COLLECT_TRXNS), [this]() { closeMainRound(); });
  }
}

HashVector Solver::getMyVector() const {
  return hvector;
}

HashMatrix Solver::getMyMatrix() const {
  return (m_generals->getMatrix());
}

void Solver::flushTransactions() {
  if (m_node->getMyLevel() != NodeLevel::Normal) {
    return;
  }

  cs::Lock lock(m_sharedMutex);

  for (auto& packet : m_transactionsBlock) {
    auto trxCount = packet.transactionsCount();

    if (trxCount != 0 && packet.isHashEmpty()) {
      packet.makeHash();

      m_node->sendTransactionsPacket(packet);
      sentTransLastRound = true;

      auto hash = packet.hash();

      if (!m_hashTable.contains(hash)) {
        m_hashTable.insert(std::move(hash), std::move(packet));
      } else {
        cslog() << "Transaction compose failed";
      }
    }
  }

  if (!m_transactionsBlock.empty()) {
    cslog() << "All transaction packets flushed, packet count: " << m_transactionsBlock.size();
    m_transactionsBlock.clear();
  }
}

bool Solver::getIPoolClosed() {
  return m_isPoolClosed;
}

void Solver::gotTransaction(csdb::Transaction&& transaction) {  // reviewer: "Need to refactoring!"
  if (m_isPoolClosed) {
    csdebug() << "m_isPoolClosed already, cannot accept your transactions";
    return;
  }

  if (transaction.is_valid()) {
#ifndef SPAMMER
    auto bytes = transaction.to_byte_stream_for_sig();

    auto vec = transaction.source().public_key();

    const std::size_t keyLength = 32;
    uint8_t           public_key[keyLength];

    for (std::size_t i = 0; i < keyLength; i++) {
      public_key[i] = vec[i];
    }

    std::string sig_str   = transaction.signature();
    uint8_t*    signature = reinterpret_cast<uint8_t*>(const_cast<char*>(sig_str.c_str()));

    if (verify_signature(signature, public_key, bytes.data(), bytes.size())) {
#endif
      v_pool.add_transaction(transaction);
#ifndef SPAMMER
    } else {
      cserror() << "Wrong signature";
    }
#endif
  } else {
    csdebug() << "Invalid transaction received";
  }
}

void Solver::gotTransactionsPacket(cs::TransactionsPacket&& packet) {
  csdebug() << "Got transaction packet";
  cs::TransactionsPacketHash hash = packet.hash();

  if (!m_hashTable.contains(hash))
    m_hashTable.insert(std::move(hash), std::move(packet));
}

void Solver::gotPacketHashesRequest(std::vector<cs::TransactionsPacketHash>&& hashes, const PublicKey& sender) {
  csdebug() << "Got transactions hash request, try to find in hash table";

  for (const auto& hash : hashes) {
    if (m_hashTable.contains(hash)) {
      m_node->sendPacketHashesReply(m_hashTable.find(hash), sender);

      csdebug() << "Found hash in hash table, send to requester";
    }
  }
}

void Solver::initConfRound() {
}

void Solver::gotPacketHashesReply(cs::TransactionsPacket&& packet) {
  csdebug() << "Got packet hash reply";
  cslog() << "Got packet hash reply";

  cs::TransactionsPacketHash hash = packet.hash();

  if (!m_hashTable.contains(hash)) {
    m_hashTable.insert(hash, std::move(packet));
  }

  {
    cs::Lock lock(m_sharedMutex);

    auto it = std::find(m_neededHashes.begin(), m_neededHashes.end(), hash);

    if (it != m_neededHashes.end()) {
      m_neededHashes.erase(it);
    }

    if (m_neededHashes.empty()) {
      buildTransactionList();
    }
  }
}

void Solver::gotRound(cs::RoundTable&& round) {
  cslog() << "Solver Got round table";

  cs::Hashes localHashes = round.hashes;
  cs::Hashes neededHashes;

  {
    cs::Lock lock(m_sharedMutex);
    m_roundTable = std::move(round);
  }

  for (const auto& hash : localHashes) {
    if (!m_hashTable.contains(hash)) {
      neededHashes.push_back(std::move(hash));
    }
  }

  if (!neededHashes.empty()) {
    m_node->sendPacketHashesRequest(neededHashes);
  } else if (m_node->getMyLevel() == NodeLevel::Confidant) {
    cs::Lock lock(m_sharedMutex);
    buildTransactionList();
  }

  {
    cs::Lock lock(m_sharedMutex);
    m_neededHashes = std::move(neededHashes);
  }
}

void Solver::buildTransactionList() {
  cslog() << "BuildTransactionlist";
  csdb::Pool pool = csdb::Pool{};  // FIX TO PACKET

  for (const auto& hash : m_roundTable.hashes) {
    if (!m_hashTable.contains(hash)) {
      cserror() << "Build vector: HASH NOT FOUND";
      return;
    }

    const auto& transactions = m_hashTable.find(hash).transactions();

    for (const auto& transaction : transactions) {
      pool.add_transaction(transaction);
    }
  }

  cs::Hash result = m_generals->buildVector(pool, m_pool);

  receivedVecFrom[m_node->getMyConfNumber()] = true;

  hvector.sender = m_node->getMyConfNumber();
  hvector.hash   = result;

  receivedVecFrom[m_node->getMyConfNumber()] = true;

  m_generals->addVector(hvector);
  m_node->sendVector(hvector);

  trustedCounterVector++;

  if (trustedCounterVector == m_roundTable.confidants.size()) {
    vectorComplete = true;

    memset(receivedVecFrom, 0, 100);
    trustedCounterVector = 0;

    // compose and send matrix!!!
    m_generals->addSenderToMatrix(m_node->getMyConfNumber());

    receivedMatFrom[m_node->getMyConfNumber()] = true;
    ++trustedCounterMatrix;

    m_node->sendMatrix(m_generals->getMatrix());
    m_generals->addMatrix(m_generals->getMatrix(), m_node->getConfidants());  // MATRIX SHOULD BE DECOMPOSED HERE!!!

    csdebug() << "SOLVER> Matrix added";
  }
}

void Solver::sendZeroVector() {
  if (transactionListReceived && !getBigBangStatus()) {
    return;
  }
}

void Solver::gotVector(HashVector&& vector) {
  cslog() << "SOLVER> GotVector";
  if (receivedVecFrom[vector.sender] == true) {
    cslog() << "SOLVER> I've already got the vector from this Node";
    return;
  }

  const std::vector<PublicKey>& confidants = m_roundTable.confidants;
  uint8_t numGen = static_cast<uint8_t>(confidants.size());

  receivedVecFrom[vector.sender] = true;

  m_generals->addVector(vector);  // building matrix
  trustedCounterVector++;

  if (trustedCounterVector == numGen) {
    vectorComplete = true;
    memset(receivedVecFrom, 0, 100);
    trustedCounterVector = 0;
    // compose and send matrix!!!
    uint8_t confNumber = m_node->getMyConfNumber();
    m_generals->addSenderToMatrix(confNumber);
    receivedMatFrom[confNumber] = true;
    trustedCounterMatrix++;

    HashMatrix matrix = m_generals->getMatrix();
    m_node->sendMatrix(matrix);
    m_generals->addMatrix(matrix, confidants);  // MATRIX SHOULD BE DECOMPOSED HERE!!!

    if (trustedCounterMatrix == numGen) {
      memset(receivedMatFrom, 0, 100);
      m_writerIndex        = (m_generals->takeDecision(m_roundTable.confidants,
                                               m_node->getBlockChain().getHashBySequence(m_node->getRoundNumber() - 1)));
      trustedCounterMatrix = 0;

      if (m_writerIndex == 100) {
        cslog() << "SOLVER> CONSENSUS WASN'T ACHIEVED!!!";
        cs::Utils::runAfter(std::chrono::milliseconds(TIME_TO_COLLECT_TRXNS), [this]() { writeNewBlock(); });
      } else {
        cslog() << "SOLVER> m_writerIndex = " << static_cast<int>(m_writerIndex);
        consensusAchieved = true;

        if (m_writerIndex == m_node->getMyConfNumber()) {
          m_node->becomeWriter();
          cs::Utils::runAfter(std::chrono::milliseconds(TIME_TO_COLLECT_TRXNS), [this]() {
            PoolMetaInfo poolMetaInfo;
            poolMetaInfo.timestamp      = cs::Utils::currentTimestamp();
            poolMetaInfo.sequenceNumber = 1 + m_node->getBlockChain().getLastWrittenSequence();

            const Characteristic&       characteristic = m_generals->getCharacteristic();
            const std::vector<uint8_t>& mask           = characteristic.mask;
            uint32_t                    bitsCount      = characteristic.size;

            m_node->sendCharacteristic(poolMetaInfo, bitsCount, mask);
            writeNewBlock();
          });
        }
      }
    }
  }

  cslog() << "Solver>  VECTOR GOT SUCCESSFULLY!!!";
}

void Solver::checkMatrixReceived() {
  if (trustedCounterMatrix < 2) {
    m_node->sendMatrix(m_generals->getMatrix());
  }
}

void Solver::setRNum(size_t _rNum) {
  rNum = static_cast<uint32_t>(_rNum);
}

void Solver::checkVectorsReceived(size_t _rNum) const {
  if (_rNum < rNum) {
    return;
  }

  const uint8_t numGen = static_cast<uint8_t>(m_node->getConfidants().size());

  if (trustedCounterVector == numGen) {
    return;
  }
}

void Solver::gotMatrix(HashMatrix&& matrix) {
  if (gotBlockThisRound) {
    return;
  }

  if (receivedMatFrom[matrix.sender]) {
    cslog() << "SOLVER> I've already got the matrix from this Node";
    return;
  }

  receivedMatFrom[matrix.sender] = true;
  trustedCounterMatrix++;
  m_generals->addMatrix(matrix, m_node->getConfidants());

  const uint8_t numGen = static_cast<uint8_t>(m_node->getConfidants().size());
  if (trustedCounterMatrix == numGen) {
    memset(receivedMatFrom, 0, 100);
    uint8_t writerIndex  = (m_generals->takeDecision(
        m_roundTable.confidants, m_node->getBlockChain().getHashBySequence(m_node->getRoundNumber() - 1)));
    trustedCounterMatrix = 0;

    if (writerIndex == 100) {
      cslog() << "SOLVER> CONSENSUS WASN'T ACHIEVED!!!";
      // cs::Utils::runAfter(std::chrono::milliseconds(TIME_TO_COLLECT_TRXNS), [this]() { writeNewBlock(); });
    } else {
      cslog() << "SOLVER> writerIndex = " << static_cast<int>(writerIndex);
      consensusAchieved = true;

      if (writerIndex == m_node->getMyConfNumber()) {
        m_node->becomeWriter();
        cs::Utils::runAfter(std::chrono::milliseconds(TIME_TO_COLLECT_TRXNS), [this]() {
          PoolMetaInfo poolMetaInfo;
          poolMetaInfo.timestamp      = cs::Utils::currentTimestamp();
          poolMetaInfo.sequenceNumber = 1 + m_node->getBlockChain().getLastWrittenSequence();

          const Characteristic& characteristic = m_generals->getCharacteristic();

          const std::vector<uint8_t>& mask = characteristic.mask;

          uint32_t bitsCount = characteristic.size;

          m_node->sendCharacteristic(poolMetaInfo, bitsCount, mask);

          m_pool.set_sequence(poolMetaInfo.sequenceNumber);
          m_pool.add_user_field(0, poolMetaInfo.timestamp);

          writeNewBlock();
        });
      }
    }
  }
}

void Solver::writeNewBlock() {
  cslog() << "Solver -> writeNewBlock ... start";

  if (consensusAchieved && m_node->getMyLevel() == NodeLevel::Writer) {
    m_node->getBlockChain().putBlock(m_pool);
    m_node->getBlockChain().setGlobalSequence(static_cast<uint32_t>(m_pool.sequence()));

    b_pool.set_sequence((m_node->getBlockChain().getLastWrittenSequence()) + 1);

    auto prev_hash = csdb::PoolHash::from_string("");
    b_pool.set_previous_hash(prev_hash);

    consensusAchieved = false;
  }
}

void Solver::gotBlock(csdb::Pool&& block, const PublicKey& sender) {
  if (m_node->getMyLevel() == NodeLevel::Writer) {
    LOG_WARN("Writer nodes don't get blocks");
    return;
  }
  gotBigBang        = false;
  gotBlockThisRound = true;
#ifdef MONITOR_NODE
  addTimestampToPool(block);
#endif
  uint32_t g_seq = block.sequence();
  csdebug() << "GOT NEW BLOCK: global sequence = " << g_seq;

  if (g_seq > m_node->getRoundNumber())
    return;  // remove this line when the block candidate signing of all trusted will be implemented

  m_node->getBlockChain().setGlobalSequence(g_seq);
  if (g_seq == m_node->getBlockChain().getLastWrittenSequence() + 1) {
    std::cout << "Solver -> getblock calls writeLastBlock" << std::endl;
    if (block.verify_signature())  // INCLUDE SIGNATURES!!!
    {
      m_node->getBlockChain().putBlock(block);
#ifndef MONITOR_NODE
      if ((m_node->getMyLevel() != NodeLevel::Writer) && (m_node->getMyLevel() != NodeLevel::Main)) {
        std::string test_hash = m_node->getBlockChain().getLastWrittenHash().to_string();
        // HASH!!!
        m_node->sendHash(test_hash, sender);
        csdebug() << "SENDING HASH: " << cs::Utils::debugByteStreamToHex(test_hash.data(), 32);
      }
#endif
    }
  }
}

void Solver::gotIncorrectBlock(csdb::Pool&& block, const PublicKey& sender) {
  std::cout << __func__ << std::endl;
  if (tmpStorage.count(block.sequence()) == 0) {
    tmpStorage.emplace(block.sequence(), block);
    std::cout << "GOTINCORRECTBLOCK> block saved to temporary storage: " << block.sequence() << std::endl;
  }
}

void Solver::gotFreeSyncroBlock(csdb::Pool&& block) {
  std::cout << __func__ << std::endl;
  if (rndStorage.count(block.sequence()) == 0) {
    rndStorage.emplace(block.sequence(), block);
    std::cout << "GOTFREESYNCROBLOCK> block saved to temporary storage: " << block.sequence() << std::endl;
  }
}

void Solver::rndStorageProcessing() {
  std::cout << __func__ << std::endl;
  bool   loop = true;
  size_t newSeq;

  while (loop) {
    newSeq = m_node->getBlockChain().getLastWrittenSequence() + 1;

    if (rndStorage.count(newSeq) > 0) {
      m_node->getBlockChain().putBlock(rndStorage.at(newSeq));
      rndStorage.erase(newSeq);
    } else
      loop = false;
  }
}

void Solver::tmpStorageProcessing() {
  std::cout << __func__ << std::endl;
  bool   loop = true;
  size_t newSeq;

  while (loop) {
    newSeq = m_node->getBlockChain().getLastWrittenSequence() + 1;

    if (tmpStorage.count(newSeq) > 0) {
      m_node->getBlockChain().putBlock(tmpStorage.at(newSeq));
      tmpStorage.erase(newSeq);
    } else
      loop = false;
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
  csunused(_pool);
  csunused(sender);
}

void Solver::gotBlockCandidate(csdb::Pool&& block) {
  csdebug() << "Solver -> getBlockCanditate";
  csunused(block);

  if (blockCandidateArrived) {
    return;
  }

  blockCandidateArrived = true;
}

void Solver::gotHash(std::string&& hash, const PublicKey& sender) {
  if (round_table_sent) {
    return;
  }

  std::string myHash = m_node->getBlockChain().getLastWrittenHash().to_string();

  cslog() << "Solver -> My Hash: " << myHash;
  cslog() << "Solver -> Received hash:" << hash;

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

    {
      auto lockTable = m_hashTable.lock_table();

      for (auto& it : lockTable) {
        hashes.push_back(it.first);
      }
    }

    m_roundTable.round++;
    m_roundTable.confidants = std::move(ips);
    m_roundTable.general    = m_node->getMyPublicKey();
    m_roundTable.hashes     = std::move(hashes);

    ips.clear();

    cslog() << "Solver -> NEW ROUND initialization done";

    m_node->initNextRound(m_roundTable);
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
        // transaction.set_comission(csdb::Amount(0, 1, 10));
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
  cs::Lock lock(m_sharedMutex);
  m_transactionsBlock.back().addTransaction(transaction);
}

void Solver::addInitialBalance() {
  cslog() << "===SETTING DB===";

  const std::string start_address = "0000000000000000000000000000000000000000000000000000000000000002";
  csdb::Pool        pool;
  csdb::Transaction transaction;
  transaction.set_target(csdb::Address::from_public_key((char*)myPublicKey.data()));
  transaction.set_source(csdb::Address::from_string(start_address));

  transaction.set_currency(csdb::Currency("CS"));
  transaction.set_amount(csdb::Amount(10000, 0));
  transaction.set_balance(csdb::Amount(10000000, 0));
  transaction.set_innerID(1);

  addTransaction(transaction);

#ifdef SPAMMER
  spamThread = std::thread(&Solver::spamWithTransactions, this);
  spamThread.detach();
#endif
}

cs::RoundNumber Solver::currentRoundNumber() {
  return m_roundTable.round;
}

const cs::RoundTable& Solver::roundTable() const {
  return m_roundTable;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// gotBlockRequest
void Solver::gotBlockRequest(csdb::PoolHash&& hash, const PublicKey& nodeId) {
  csdb::Pool pool = m_node->getBlockChain().loadBlock(hash);
  if (pool.is_valid()) {
    auto prev_hash = csdb::PoolHash::from_string("");
    pool.set_previous_hash(prev_hash);
    m_node->sendBlockReply(pool, nodeId);
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// gotBlockReply
void Solver::gotBlockReply(csdb::Pool&& pool) {
  cslog() << "Solver -> Got Block for my Request: " << pool.sequence();
  if (pool.sequence() == m_node->getBlockChain().getLastWrittenSequence() + 1)
    m_node->getBlockChain().putBlock(pool);
}

void Solver::addConfirmation(uint8_t confNumber_) {
  if (writingConfGotFrom[confNumber_]) {
    return;
  }
  writingConfGotFrom[confNumber_] = true;
  writingCongGotCurrent++;
  if (writingCongGotCurrent == 2) {
    m_node->becomeWriter();
    cs::Utils::runAfter(std::chrono::milliseconds(TIME_TO_COLLECT_TRXNS), [this]() { writeNewBlock(); });
  }
}

void Solver::nextRound() {
  cslog() << "SOLVER> next Round : Starting ... nextRound";

  receivedVec_ips.clear();
  receivedMat_ips.clear();

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

  if (m_node->getMyLevel() == NodeLevel::Confidant) {
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
  cs::Lock lock(m_sharedMutex);

  if (m_transactionsBlock.empty()) {
    m_transactionsBlock.push_back(cs::TransactionsPacket{});
  }

  if (m_transactionsBlock.back().transactionsCount() >= MaxPacketTransactions) {
    m_transactionsBlock.push_back(cs::TransactionsPacket{});
  }

  m_transactionsBlock.back().addTransaction(transaction);
}

const std::vector<uint8_t>& Solver::getPrivateKey() const {
  return myPrivateKey;
}

}  // namespace cs
