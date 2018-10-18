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
#include <sys/timeb.h>

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

#if defined(SPAMMER)
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

void Solver::setKeysPair(const cs::PublicKey& publicKey, const cs::PrivateKey& privateKey) {
  m_publicKey = publicKey;
  m_privateKey = privateKey;
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

std::optional<csdb::Pool> Solver::applyCharacteristic(const cs::Characteristic& characteristic, const PoolMetaInfo& metaInfoPool,
                                 const PublicKey& sender) {
  cslog() << "SOLVER> ApplyCharacteristic";

  gotBigBang = false;
  gotBlockThisRound = true;

  cs::Lock lock(m_sharedMutex);
  cs::Hashes localHashes = m_roundTable.hashes;

  cslog() << "Solver> Characteristic bytes size " << characteristic.mask.size();
  csdebug() << "Solver> Characteristic bytes " << cs::Utils::debugByteStreamToHex(characteristic.mask.data(), characteristic.mask.size());

  csdb::Pool newPool;
  std::size_t maskIndex = 0;
  const cs::Bytes& mask = characteristic.mask;

  for (const auto& hash : localHashes) {
    if (!m_hashTable.count(hash)) {
      cserror() << "SOLVER> ApplyCharacteristic: HASH NOT FOUND " << hash.toString();
      return std::nullopt;
    }

    const auto& transactions = m_hashTable[hash].transactions();

    for (const auto& transaction : transactions) {
      if (mask.at(maskIndex)) {
        newPool.add_transaction(transaction);
      }

      ++maskIndex;
    }
  }

  m_hashesToRemove = cs::HashesSet(localHashes.begin(), localHashes.end());

  if (characteristic.size != newPool.transactions_count()) {
    cslog() << "Characteristic size: " << characteristic.size << ", new pool transactions count: " << newPool.transactions_count();
    cswarning() << "SOLVER> ApplyCharacteristic: Some of transactions is not valid";
  }
                                                           
  cslog() << "SOLVER> ApplyCharacteristic : sequence = " << metaInfoPool.sequenceNumber;

  newPool.set_sequence(metaInfoPool.sequenceNumber);
  newPool.add_user_field(0, metaInfoPool.timestamp);

  // TODO: need to write confidants notifications bytes to csdb::Pool user fields
#ifdef MONITOR_NODE
  addTimestampToPool(newPool);
#endif

  return newPool;
}

const Characteristic& Solver::getCharacteristic() const {
  return m_generals->getCharacteristic();
}

Hash Solver::getCharacteristicHash() const {
  const Characteristic& characteristic = m_generals->getCharacteristic();
  return getBlake2Hash(characteristic.mask.data(), characteristic.mask.size());
}

PublicKey Solver::getWriterPublicKey() const {
  PublicKey result;

  if (m_writerIndex < m_roundTable.confidants.size()) {
    result = m_roundTable.confidants[m_writerIndex];
  }
  else {
    cserror() << "WRITER PUBLIC KEY IS NOT EXIST AT CONFIDANTS. LOGIC ERROR!";
  }

  return result;
}

bool Solver::isPoolClosed() const {
  return m_isPoolClosed;
}

void Solver::removePreviousHashes()
{
  cs::Lock lock(m_sharedMutex);

  for (const auto& hash : m_hashesToRemove) {
    m_hashTable.erase(hash);
  }

  m_hashesToRemove.clear();
}

bool Solver::checkTableHashes(const cs::RoundTable& table)
{
  const cs::Hashes& hashes = table.hashes;
  cs::Hashes neededHashes;
  
  for (const auto& hash : hashes) {
    if (!m_hashTable.count(hash)) {
      neededHashes.push_back(hash);
    }
  }

  if (!neededHashes.empty()) {
    m_node->sendPacketHashesRequest(neededHashes);
  }

  for (const auto& hash : neededHashes) {
    csfile() << "SOLVER> Need hash >> " << hash.toString();
  }

  return neededHashes.empty();
}

HashVector Solver::getMyVector() const {
  return hvector;
}

HashMatrix Solver::getMyMatrix() const {
  return (m_generals->getMatrix());
}

void Solver::flushTransactions() {
  cs::Lock lock(m_sharedMutex);

  if (m_node->getMyLevel() != NodeLevel::Normal ||
      m_roundTable.round <= TransactionsFlushRound) {
    return;
  }

  std::size_t allTransactionsCount = 0;

  for (auto& packet : m_transactionsBlock) {
    auto transactionsCount = packet.transactionsCount();

    if (transactionsCount != 0 && packet.isHashEmpty()) {
      packet.makeHash();

      const auto& transactions = packet.transactions();

      for (const auto& transaction : transactions) {
        if (!transaction.is_valid()) {
          cswarning() << "Can not send not valid transaction, sorry";
          continue;
        }
      }

      m_node->sendTransactionsPacket(packet);

      auto hash = packet.hash();

      if (hash.isEmpty()) {
        cserror() << "Transaction packet hashing failed";
      }

      if (!m_hashTable.count(hash)) {
        m_hashTable.emplace(hash, packet);
      } else {
        cserror() << "Logical error, adding transactions packet more than one time";
      }

      allTransactionsCount += transactionsCount;
    }
  }

  if (!m_transactionsBlock.empty()) {
    csdebug() << "CONVEYER> All transaction packets flushed, packets count: " << m_transactionsBlock.size();
    csdebug() << "CONVEYER> Common flushed transactions count: " << allTransactionsCount;

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

    if (verifySignature(signature, public_key, bytes.data(), bytes.size())) {
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
  cs::Lock lock(m_sharedMutex);

  if (!m_hashTable.count(hash)) {
    m_hashTable.emplace(hash, packet);
  }
}

void Solver::gotPacketHashesRequest(std::vector<cs::TransactionsPacketHash>&& hashes, const PublicKey& sender) {
  cs::SharedLock lock(m_sharedMutex);

  for (const auto& hash : hashes) {
    if (m_hashTable.count(hash)) {
      cslog() << "Found hash" << hash.toString() << "in hash table, sending to requester";

      m_node->sendPacketHashesReply(m_hashTable[hash], sender);
    }
  }
}

void Solver::gotPacketHashesReply(cs::TransactionsPacket&& packet) {
  csfile() << "Solver> Got packet hash reply";

  cs::TransactionsPacketHash hash = packet.hash();
  cs::Lock lock(m_sharedMutex);

  if (!m_hashTable.count(hash)) {
    m_hashTable.emplace(hash, std::move(packet));
  }

  auto it = std::find(m_neededHashes.begin(), m_neededHashes.end(), hash);

  if (it != m_neededHashes.end()) {
    m_neededHashes.erase(it);
  }

  if (m_neededHashes.empty()) {
    csfile() << "Solver> Hashes received, checking hash table again";

    if (!checkTableHashes(m_roundTable)) {
      return;
    }

    if (m_node->getMyLevel() == NodeLevel::Confidant) {
      runConsensus();
    }
  }
}

void Solver::gotRound(cs::RoundTable&& round) {
  cslog() << "Solver> Got round table";

  cs::Hashes localHashes = round.hashes;
  cs::Hashes neededHashes;

  {
    cs::Lock lock(m_sharedMutex);
    m_roundTable = std::move(round);
  }

  for (const auto& hash : localHashes) {
    if (!m_hashTable.count(hash)) {
      neededHashes.push_back(std::move(hash));
    }
  }

  if (!neededHashes.empty()) {
    m_node->sendPacketHashesRequest(neededHashes);
  }
  else if (m_node->getMyLevel() == NodeLevel::Confidant) {
    cs::Timer::singleShot(TIME_TO_AWAIT_ACTIVITY, [this] {
      cs::Lock lock(m_sharedMutex);
      runConsensus();
    });
  }
  else {
    cslog() << "All round transactions packet hashes in table";
  }

  {
    cs::Lock lock(m_sharedMutex);
    m_neededHashes = std::move(neededHashes);
  }
}

void Solver::runConsensus() {
  if (isConsensusRunning) {
    return;
  }

  isConsensusRunning = true;

  cslog() << "SOLVER> Run Consensus";
  cs::TransactionsPacket packet;

  for (const auto& hash : m_roundTable.hashes) {
    if (!m_hashTable.count(hash)) {
      cserror() << "Consensus build vector: HASH NOT FOUND";
      return;
    }

    const auto& transactions = m_hashTable[hash].transactions();

    for (const auto& transaction : transactions) {
      if (!packet.addTransaction(transaction)) {
        cserror() << "Can not add transaction to packet in consensus";
      }
    }
  }

  cslog() << "SOLVER> Consensus transaction packet of " << packet.transactionsCount() << " transactions";

  cs::Hash result = m_generals->buildVector(packet);

  receivedVecFrom[m_node->getMyConfNumber()] = true;

  hvector.sender = m_node->getMyConfNumber();
  hvector.hash = result;

  receivedVecFrom[m_node->getMyConfNumber()] = true;

  m_generals->addVector(hvector);
  m_node->sendVector(hvector);

  trustedCounterVector++;

  if (trustedCounterVector == m_roundTable.confidants.size()) {

    memset(receivedVecFrom, 0, 100);
    trustedCounterVector = 0;

    // compose and send matrix!!!
    m_generals->addSenderToMatrix(m_node->getMyConfNumber());

    receivedMatFrom[m_node->getMyConfNumber()] = true;
    ++trustedCounterMatrix;

    m_node->sendMatrix(m_generals->getMatrix());
    m_generals->addMatrix(m_generals->getMatrix(), m_roundTable.confidants);  // MATRIX SHOULD BE DECOMPOSED HERE!!!

    cslog() << "SOLVER> Matrix added";
  }
}

void Solver::runFinalConsensus() {
  const uint8_t numGen = static_cast<uint8_t>(m_roundTable.confidants.size());

  if (trustedCounterMatrix == numGen) {
    std::memset(receivedMatFrom, 0, sizeof(receivedMatFrom));

    m_writerIndex = (m_generals->takeDecision(m_roundTable.confidants,
                                              m_node->getBlockChain().getHashBySequence(m_node->getRoundNumber() - 1)));
    trustedCounterMatrix = 0;

    if (m_writerIndex == 100) {
      cslog() << "SOLVER> CONSENSUS WASN'T ACHIEVED!!!";
    }
    else {
      cslog() << "SOLVER> CONSENSUS ACHIEVED!!!";
      cslog() << "SOLVER> m_writerIndex = " << static_cast<int>(m_writerIndex);

      if (m_writerIndex == m_node->getMyConfNumber()) {
        m_node->becomeWriter();
      }
      else {
        // TODO: make next stage without delay
        cs::Timer::singleShot(TIME_TO_AWAIT_ACTIVITY, [this] {
          m_node->sendWriterNotification();
        });
      }
    }
  }
}

void Solver::gotVector(HashVector&& vector) {
  cslog() << "SOLVER> GotVector";

  if (receivedVecFrom[vector.sender] == true) {
    cslog() << "SOLVER> I've already got the vector from this Node";
    return;
  }

  const cs::ConfidantsKeys &confidants = m_roundTable.confidants;
  uint8_t numGen = static_cast<uint8_t>(confidants.size());

  receivedVecFrom[vector.sender] = true;

  m_generals->addVector(vector);  // building matrix
  trustedCounterVector++;

  if (trustedCounterVector == numGen) {
    std::memset(receivedVecFrom, 0, sizeof(receivedVecFrom));
    trustedCounterVector = 0;
    // compose and send matrix!!!
    uint8_t confNumber = m_node->getMyConfNumber();
    m_generals->addSenderToMatrix(confNumber);
    receivedMatFrom[confNumber] = true;
    trustedCounterMatrix++;

    HashMatrix matrix = m_generals->getMatrix();
    m_node->sendMatrix(matrix);
    m_generals->addMatrix(matrix, confidants);  // MATRIX SHOULD BE DECOMPOSED HERE!!!

    runFinalConsensus();
  }

  cslog() << "Solver>  VECTOR GOT SUCCESSFULLY!!!";
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
  m_generals->addMatrix(matrix, m_roundTable.confidants);

  runFinalConsensus();
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
  uint32_t g_seq = cs::numeric_cast<uint32_t>(block.sequence());
  csdebug() << "GOT NEW BLOCK: global sequence = " << g_seq;

  if (g_seq > m_node->getRoundNumber())
    return;  // remove this line when the block candidate signing of all trusted will be implemented

  m_node->getBlockChain().setGlobalSequence(g_seq);
  if (g_seq == m_node->getBlockChain().getLastWrittenSequence() + 1) {
    cslog() << "Solver -> getblock calls writeLastBlock";
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
  cslog() << __func__;
  if (tmpStorage.count(block.sequence()) == 0) {
    tmpStorage.emplace(block.sequence(), block);
    cslog() << "GOTINCORRECTBLOCK> block saved to temporary storage: " << block.sequence();
  }
}

void Solver::gotFreeSyncroBlock(csdb::Pool&& block) {
  cslog() << __func__;
  if (rndStorage.count(block.sequence()) == 0) {
    rndStorage.emplace(block.sequence(), block);
    cslog() << "GOTFREESYNCROBLOCK> block saved to temporary storage: " << block.sequence();
  }
}

void Solver::rndStorageProcessing() {
  cslog() << __func__;
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
  cslog() << __func__;
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

  cslog() << "Solver -> Received public key: " << sender.data();

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
      cs::SharedLock lock(m_sharedMutex);

      for (const auto& element : m_hashTable) {
        const auto iterator = std::find(m_hashesToRemove.begin(), m_hashesToRemove.end(), element.first);

        if (iterator == m_hashesToRemove.end()) {
          hashes.push_back(element.first);
        }
      }
    }

    m_roundTable.round++;
    m_roundTable.confidants = std::move(ips);
    m_roundTable.general = m_node->getMyPublicKey();
    m_roundTable.hashes = std::move(hashes);

    ips.clear();

    cslog() << "Solver -> NEW ROUND initialization done";

    cs::Timer::singleShot(cs::RoundDelay, [this]() {
      m_node->initNextRound(m_roundTable);
      round_table_sent = true;
    });
  }
}

/////////////////////////////
#ifdef SPAMMER
void Solver::spamWithTransactions() {
  cslog() << "STARTING SPAMMER...";
  std::string mp = "1234567890abcdef";

  uint64_t iid = 0;
  std::this_thread::sleep_for(std::chrono::seconds(5));

  auto aaa = csdb::Address::from_string("0000000000000000000000000000000000000000000000000000000000000001");
  auto bbb = csdb::Address::from_string("0000000000000000000000000000000000000000000000000000000000000002");

  csdb::Transaction transaction;
  transaction.set_target(aaa);
  transaction.set_source(csdb::Address::from_public_key((char*)m_publicKey.data()));
  transaction.set_currency(csdb::Currency("CS"));

  const cs::RoundNumber round = m_roundTable.round;
  const std::size_t minTransactionsCount = 50;
  const std::size_t maxTransactionsCount = 100;

  // TODO: fix magic values
  while (true) {
    if (spamRunning && (m_node->getMyLevel() == Normal)) {
      const std::size_t transactionsCount = cs::Utils::generateRandomValue(minTransactionsCount, maxTransactionsCount);

      for (std::size_t i = 0; i < transactionsCount; ++i) {
        transaction.set_amount(csdb::Amount(randFT(1, 1000), 0));
        // transaction.set_comission(csdb::Amount(0, 1, 10));
        transaction.set_balance(csdb::Amount(transaction.amount().integral() + 2, 0));
        transaction.set_innerID(iid);
        ++iid;

        if (!transaction.is_valid()) {
          cserror() << "Generated transaction is not valid";
        }

        addConveyerTransaction(transaction);
      }
    }

    const std::size_t awaitTime = cs::Utils::generateRandomValue(TIME_TO_AWAIT_ACTIVITY, TIME_TO_AWAIT_ACTIVITY << 2);
    std::this_thread::sleep_for(std::chrono::milliseconds(awaitTime));
  }
}
#endif

///////////////////
void Solver::send_wallet_transaction(const csdb::Transaction& transaction) {
  cs::Solver::addConveyerTransaction(transaction);
}

void Solver::addInitialBalance() {
  cslog() << "===SETTING DB===";

  const std::string start_address = "0000000000000000000000000000000000000000000000000000000000000002";
  csdb::Transaction transaction;
  transaction.set_target(csdb::Address::from_public_key(reinterpret_cast<char*>(m_publicKey.data())));
  transaction.set_source(csdb::Address::from_string(start_address));

  transaction.set_currency(csdb::Currency("CS"));
  transaction.set_amount(csdb::Amount(10000, 0));
  transaction.set_balance(csdb::Amount(10000000, 0));
  transaction.set_innerID(1);

  addConveyerTransaction(transaction);
}

void Solver::runSpammer() {
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

const cs::TransactionsPacketHashTable& Solver::transactionsPacketTable() const
{
  return m_hashTable;
}

const cs::Notifications& Solver::notifications() const {
  return m_notifications;
}

void Solver::addNotification(const cs::Bytes& bytes) {
  m_notifications.push_back(bytes);
}

std::size_t Solver::neededNotifications() const {
  return m_roundTable.confidants.size() / 2;  // TODO: + 1 at the end may be?
}

bool Solver::isEnoughNotifications() const {
  const std::size_t neededConfidantsCount = neededNotifications();
  const std::size_t notificationsCount = notifications().size();

  cslog() << "Get notification, current notifications count - " << notificationsCount;
  cslog() << "Needed confidans count - " << neededConfidantsCount;

  return notificationsCount == neededConfidantsCount;
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

void Solver::nextRound() {
  cslog() << "SOLVER> next Round : Starting ... nextRound";

  ips.clear();

  m_notifications.clear();

  blockCandidateArrived = false;
  gotBlockThisRound = false;
  round_table_sent = false;
  isConsensusRunning = false;

  if (m_isPoolClosed) {
    v_pool = csdb::Pool{};
  }

  removePreviousHashes();

  if (m_node->getMyLevel() == NodeLevel::Confidant) {
    cs::Utils::clearMemory(receivedVecFrom);
    cs::Utils::clearMemory(receivedMatFrom);

    trustedCounterVector = 0;
    trustedCounterMatrix = 0;

    cslog() << "SOLVER> next Round : the variables initialized";

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

bool Solver::verifySignature(uint8_t signature[64], uint8_t public_key[32], uint8_t* message, size_t message_len) {
  int ver_ok = crypto_sign_verify_detached(signature, message, message_len, public_key);
  return ver_ok == 0;
}

void Solver::addConveyerTransaction(const csdb::Transaction& transaction) {
  cs::Lock lock(m_sharedMutex);

  if (m_transactionsBlock.empty() || (m_transactionsBlock.back().transactionsCount() >= MaxPacketTransactions)) {
    m_transactionsBlock.push_back(cs::TransactionsPacket());
  }

  m_transactionsBlock.back().addTransaction(transaction);
}

const cs::PrivateKey& Solver::getPrivateKey() const {
  return m_privateKey;
}

const cs::PublicKey& Solver::getPublicKey() const {
  return m_publicKey;
}

}  // namespace cs
