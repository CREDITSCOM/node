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
#include <cassert>

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

static const int NUM_OF_SPAM_KEYS = 10;

#endif
}  // namespace

namespace cs {
constexpr short min_nodes = 3;

Solver::Solver(Node* node, csdb::Address _genesisAddress, csdb::Address _startAddress
#ifdef SPAMMER
  , csdb::Address _spammerAddress
#endif
  )
: m_node(node)
, m_walletsState(new WalletsState(node->getBlockChain()))
, m_generals(std::unique_ptr<Generals>(new Generals(*m_walletsState)))
, m_genesisAddress(_genesisAddress)
, m_startAddress(_startAddress)
#ifdef SPAMMER
, m_spammerAddress(_spammerAddress)
#endif
, m_feeCounter()
, m_writerIndex(0) {
  m_hashTablesStorage.resize(HashTablesStorageCapacity, { });
  m_sendingPacketTimer.connect(std::bind(&Solver::flushTransactions, this));
#ifdef SPAMMER
  uint8_t sk[64];
  uint8_t pk[32];
  csdb::Address pub;
  for (int i = 0; i < NUM_OF_SPAM_KEYS; i++)
  {
    crypto_sign_keypair(pk, sk);
    pub = pub.from_public_key((const char*)pk);
    m_spamKeys.push_back(pub);
  }
#endif
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
  if (m_gotBigBang) {
    return;
  }

  uint32_t tNum = static_cast<uint32_t>(m_vPool.transactions_count());

  cslog() << "AAAAAAAAAAAAAAAAAAAAAAAA -= TRANSACTION RECEIVING IS OFF =- AAAAAAAAAAAAAAAAAAAAAAAAAAAA";
  csdebug() << "                          Total received " << tNum << " transactions";
  cslog() << "========================================================================================";

  m_isPoolClosed = true;

  cslog() << "Solver -> Sending " << tNum << " transactions ";

  m_vPool.set_sequence(m_node->getRoundNumber());
  m_node->sendTransactionList(m_vPool);  // Correct sending, better when to all one time
}

uint32_t Solver::getTLsize() {
  return static_cast<uint32_t>(m_vPool.transactions_count());
}

std::optional<csdb::Pool> Solver::applyCharacteristic(const cs::Characteristic& characteristic,
    const PoolMetaInfo& metaInfoPool, const PublicKey& sender) {
  cslog() << "SOLVER> ApplyCharacteristic";

  m_gotBigBang = false;
  m_gotBlockThisRound = true;

  cs::Lock lock(m_sharedMutex);

  cs::Hashes localHashes = m_roundTable.hashes;
  cs::TransactionsPacketHashTable hashTable;

  cslog() << "Solver> Characteristic bytes size " << characteristic.mask.size();

  csdb::Pool newPool;
  std::size_t maskIndex = 0;
  const cs::Bytes& mask = characteristic.mask;

  for (const auto& hash : localHashes) {
    if (!m_hashTable.count(hash)) {
      cserror() << "SOLVER> ApplyCharacteristic: HASH NOT FOUND " << hash.toString();
      return std::nullopt;
    }

    auto& packet = m_hashTable[hash];
    const auto& transactions = packet.transactions();

    for (const auto& transaction : transactions) {
      if (mask.at(maskIndex)) {
        newPool.add_transaction(transaction);
      }

      ++maskIndex;
    }

    // create storage hash table and remove from current hash table
    hashTable.emplace(hash, std::move(packet));
    m_hashTable.erase(hash);
  }

  cs::StorageElement element;
  element.round = m_roundTable.round;
  element.hashTable = std::move(hashTable);

  // add current round hashes to storage
  m_hashTablesStorage.push_back(element);

  if (characteristic.mask.size() != newPool.transactions_count()) {
    cslog() << "Characteristic size: " << characteristic.mask.size() << ", new pool transactions count: " << newPool.transactions_count();
    cswarning() << "SOLVER> ApplyCharacteristic: Some of transactions is not valid";
  }
                                                           
  cslog() << "SOLVER> ApplyCharacteristic : sequence = " << metaInfoPool.sequenceNumber;

  newPool.set_sequence(metaInfoPool.sequenceNumber);
  newPool.add_user_field(0, metaInfoPool.timestamp);

  const auto& writer_public_key = sender;
  newPool.set_writer_public_key(csdb::internal::byte_array(writer_public_key.begin(), writer_public_key.end()));
  m_feeCounter.CountFeesInPool(m_node, &newPool);
  m_node->getBlockChain().finishNewBlock(newPool);

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

cs::PublicKey Solver::writerPublicKey() const {
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
    csdebug() << "SOLVER> Need hash >> " << hash.toString();
  }

  return neededHashes.empty();
}

bool Solver::isPacketSyncFinished() const {
  return m_neededHashes.empty();
}

const cs::Hashes& Solver::getNeededHashes() const {
  return m_neededHashes;
}

HashVector Solver::hashVector() const {
  return m_hashVector;
}

HashMatrix Solver::hashMatrix() const {
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

bool Solver::isPoolClosed() {
  return m_isPoolClosed;
}

void Solver::gotTransaction(csdb::Transaction&& transaction) {  // reviewer: "Need to refactoring!"
  if (m_isPoolClosed) {
    csdebug() << "m_isPoolClosed already, cannot accept your transactions";
    return;
  }

  if (transaction.is_valid()) {
      m_vPool.add_transaction(transaction);
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

void Solver::gotPacketHashesRequest(cs::Hashes&& hashes, const RoundNumber round, const PublicKey& sender) {
  cs::SharedLock lock(m_sharedMutex);

  std::size_t foundHashesCount = 0;
  const auto senderHex = cs::Utils::byteStreamToHex(sender.data(), sender.size());

  for (const auto& hash : hashes) {
    if (m_hashTable.count(hash)) {
      csdebug() << "SOLVER> Found hash at current table in request - " << hash.toString();
      csdebug() << "SOLVER> Sending hash to requester: " << senderHex;

      m_node->sendPacketHashesReply(m_hashTable[hash], sender);

      ++foundHashesCount;
    }
  }

  if (foundHashesCount == hashes.size()) {
    cslog() << "SOVLER> All requested hashes found at current table";
    return;
  }

  cswarning() << "SOLVER> Not all hashes found in current hash table, searching at hash table storage";

  const auto iterator = std::find_if(m_hashTablesStorage.begin(),
                                     m_hashTablesStorage.end(),
                                     [&, this](const cs::StorageElement& element) {
                                       return element.round == round;
                                     });

  if (iterator != m_hashTablesStorage.end()) {
    csdebug() << "SOLVER> Found round hash table in storage, searching hashes";

    for (const auto& hash : hashes) {
      if (iterator->hashTable.count(hash)) {
        csdebug() << "SOLVER> Found hash at storage, sending to requester " << senderHex;
        m_node->sendPacketHashesReply(iterator->hashTable[hash], sender);

        ++foundHashesCount;
      }
    }

    if (foundHashesCount == hashes.size()) {
      csdebug() << "SOVLER> All requested hashes found at storage";
    }
  }
  else {
    csdebug() << "SOLVER> can not find round in storage, hashes not found";
    m_node->sendPacketHashesReply(cs::TransactionsPacket(), sender);
  }
}

void Solver::gotPacketHashesReply(cs::TransactionsPacket&& packet) {
  csdebug() << "SOLVER> Got packet hash reply";

  cs::TransactionsPacketHash hash = packet.hash();
  cs::Lock lock(m_sharedMutex);

  auto it = std::find(m_neededHashes.begin(), m_neededHashes.end(), hash);

  // add needed packet and hash to hash table if it was in needed hashes
  if (it != m_neededHashes.end()) {
    m_neededHashes.erase(it);
    m_hashTable.emplace(hash, std::move(packet));

    cslog() << "SOLVER> Sync packet added to current hash table";
  }

  if (isPacketSyncFinished()) {
    csdebug() << "SOLVER> Hashes received, checking hash table again";
    m_node->resetNeighbours();

    if (m_node->getMyLevel() == NodeLevel::Confidant) {
      runConsensus();
    }

    const cs::RoundNumber currentRound = m_roundTable.round;

    if (isCharacteristicMetaReceived(currentRound)) {
      csdebug() << "SOLVER> Run characteristic meta";
      cs::CharacteristicMeta meta = characteristicMeta(currentRound);

      if (meta.round != 0) {
      m_node->getCharacteristic(meta.bytes.data(), meta.bytes.size(), meta.sender);
    }
      else {
        csfatal() << "SOLVER> Can not call node get characteristic method";
      }
    }
  }
}

void Solver::gotRound(cs::RoundTable&& round) {
  cslog() << "SOLVER> Got round table";

  cs::Hashes localHashes = round.hashes;
  cs::Hashes neededHashes;

  cs::Lock lock(m_sharedMutex);

  m_roundTable = std::move(round);

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
    cslog() << "SOLVER> All round transactions packet hashes in table";
  }

  m_neededHashes = std::move(neededHashes);
}

void Solver::runConsensus() {
  if (m_isConsensusRunning) {
    return;
  }

  m_isConsensusRunning = true;

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

#ifndef SPAMMER
  packet = removeTransactionsWithBadSignatures(packet);
#endif

  // TODO: fix that
  csdb::Pool pool;
  pool.transactions() = packet.transactions();

  m_feeCounter.CountFeesInPool(m_node, &pool);
  packet.clear();

  const auto& transactionsWithFees = pool.transactions();

  // TODO: transaction can be without fee?
  for (int i = 0; i < transactionsWithFees.size(); ++i) {
    packet.addTransaction(transactionsWithFees[i]);
  }

  cs::Hash result = m_generals->buildVector(packet);

  m_receivedVectorFrom[m_node->getMyConfNumber()] = true;

  m_hashVector.sender = m_node->getMyConfNumber();
  m_hashVector.hash = result;

  m_receivedVectorFrom[m_node->getMyConfNumber()] = true;

  m_generals->addVector(m_hashVector);
  m_node->sendVector(m_hashVector);

  trustedCounterVector++;

  if (trustedCounterVector == m_roundTable.confidants.size()) {
    std::memset(m_receivedVectorFrom, 0, sizeof(m_receivedVectorFrom));
    trustedCounterVector = 0;

    // compose and send matrix!!!
    m_generals->addSenderToMatrix(m_node->getMyConfNumber());

    m_receivedMatrixFrom[m_node->getMyConfNumber()] = true;
    ++trustedCounterMatrix;

    m_node->sendMatrix(m_generals->getMatrix());
    m_generals->addMatrix(m_generals->getMatrix(), m_roundTable.confidants);  // MATRIX SHOULD BE DECOMPOSED HERE!!!

    cslog() << "SOLVER> Matrix added";
  }
}

void Solver::runFinalConsensus() {
  const uint8_t numGen = static_cast<uint8_t>(m_roundTable.confidants.size());

  if (trustedCounterMatrix == numGen) {
    std::memset(m_receivedMatrixFrom, 0, sizeof(m_receivedMatrixFrom));

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

  if (m_receivedVectorFrom[vector.sender] == true) {
    cslog() << "SOLVER> I've already got the vector from this Node";
    return;
  }

  const cs::ConfidantsKeys& confidants = m_roundTable.confidants;
  uint8_t numGen = static_cast<uint8_t>(confidants.size());

  m_receivedVectorFrom[vector.sender] = true;

  m_generals->addVector(vector);  // building matrix
  trustedCounterVector++;

  if (trustedCounterVector == numGen) {
    std::memset(m_receivedVectorFrom, 0, sizeof(m_receivedVectorFrom));
    trustedCounterVector = 0;

    // compose and send matrix!!!
    uint8_t confNumber = m_node->getMyConfNumber();
    m_generals->addSenderToMatrix(confNumber);
    m_receivedMatrixFrom[confNumber] = true;

    trustedCounterMatrix++;

    HashMatrix matrix = m_generals->getMatrix();

    m_node->sendMatrix(matrix);
    m_generals->addMatrix(matrix, confidants);  // MATRIX SHOULD BE DECOMPOSED HERE!!!

    runFinalConsensus();
  }

  cslog() << "Solver>  VECTOR GOT SUCCESSFULLY!!!";
}

void Solver::gotMatrix(HashMatrix&& matrix) {
  if (m_gotBlockThisRound) {
    return;
  }

  if (m_receivedMatrixFrom[matrix.sender]) {
    cslog() << "SOLVER> I've already got the matrix from this Node";
    return;
  }

  m_receivedMatrixFrom[matrix.sender] = true;
  trustedCounterMatrix++;
  m_generals->addMatrix(matrix, m_roundTable.confidants);

  runFinalConsensus();
}

void Solver::gotBlock(csdb::Pool&& block, const PublicKey& sender) {
  if (m_node->getMyLevel() == NodeLevel::Writer) {
    LOG_WARN("Writer nodes don't get blocks");
    return;
  }
  m_gotBigBang        = false;
  m_gotBlockThisRound = true;
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
      m_node->getBlockChain().onBlockReceived(block);
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
  if (m_temporaryStorage.count(block.sequence()) == 0) {
    m_temporaryStorage.emplace(block.sequence(), block);
    cslog() << "GOTINCORRECTBLOCK> block saved to temporary storage: " << block.sequence();
  }
}

void Solver::gotFreeSyncroBlock(csdb::Pool&& block) {
  cslog() << __func__;
  if (m_randomStorage.count(block.sequence()) == 0) {
    m_randomStorage.emplace(block.sequence(), block);
    cslog() << "GOTFREESYNCROBLOCK> block saved to temporary storage: " << block.sequence();
  }
}

void Solver::rndStorageProcessing() {
  cslog() << __func__;
  bool   loop = true;
  size_t newSeq;

  while (loop) {
    newSeq = m_node->getBlockChain().getLastWrittenSequence() + 1;

    if (m_randomStorage.count(newSeq) > 0) {
      m_node->getBlockChain().onBlockReceived(m_randomStorage.at(newSeq));
      m_randomStorage.erase(newSeq);
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

    if (m_temporaryStorage.count(newSeq) > 0) {
      m_node->getBlockChain().onBlockReceived(m_temporaryStorage.at(newSeq));
      m_temporaryStorage.erase(newSeq);
    } else
      loop = false;
  }
}

bool Solver::bigBangStatus() {
  return m_gotBigBang;
}

void Solver::setBigBangStatus(bool _status) {
  m_gotBigBang = _status;
}

void Solver::gotBadBlockHandler(csdb::Pool&& _pool, const PublicKey& sender) {
  // insert code here
  csunused(_pool);
  csunused(sender);
}

void Solver::gotBlockCandidate(csdb::Pool&& block) {
  csdebug() << "Solver -> getBlockCanditate";
  csunused(block);

  if (m_blockCandidateArrived) {
    return;
  }

  m_blockCandidateArrived = true;
}

void Solver::gotHash(std::string&& hash, const PublicKey& sender) {
  if (m_roundTableSent) {
    return;
  }

  std::string myHash = m_node->getBlockChain().getLastWrittenHash().to_string();

  cslog() << "Solver -> My Hash: " << myHash;
  cslog() << "Solver -> Received hash:" << hash;

  cslog() << "Solver -> Received public key: " << sender.data();

  if (m_hashesReceivedKeys.size() <= min_nodes) {
    if (hash == myHash) {
      csdebug() << "Solver -> Hashes are good";
      m_hashesReceivedKeys.push_back(sender);
    } else {
      cslog() << "Hashes do not match!!!";
      return;
    }
  } else {
    cslog() << "Solver -> We have enough hashes!";
    return;
  }

  if ((m_hashesReceivedKeys.size() == min_nodes) && (!m_roundTableSent)) {
    cslog() << "Solver -> sending NEW ROUND table";
    cs::Hashes hashes;

    {
      cs::SharedLock lock(m_sharedMutex);

      for (const auto& element : m_hashTable) {
          hashes.push_back(element.first);
        }
      }

    m_roundTable.round++;
    m_roundTable.confidants = std::move(m_hashesReceivedKeys);
    m_roundTable.general = m_node->getMyPublicKey();
    m_roundTable.hashes = std::move(hashes);

    m_hashesReceivedKeys.clear();

    cslog() << "Solver -> NEW ROUND initialization done";

    cs::Timer::singleShot(cs::RoundDelay, [this]() {
      m_node->initNextRound(m_roundTable);
      m_roundTableSent = true;
    });
  }
}

/////////////////////////////
#ifdef SPAMMER
void Solver::spamWithTransactions()
{
  cslog() << "STARTING SPAMMER...";

  long counter = 0;
  uint64_t iid = 0;
  std::this_thread::sleep_for(std::chrono::seconds(5));

  csdb::Transaction transaction;
  transaction.set_currency(csdb::Currency(1));

  const std::size_t minTransactionsCount = 50;
  const std::size_t maxTransactionsCount = 100;

  while (true) {
    if (m_isSpamRunning && (m_node->getMyLevel() == Normal)) {
      csdb::internal::WalletId id;
      const std::size_t transactionsCount = cs::Utils::generateRandomValue(minTransactionsCount, maxTransactionsCount);

      for (std::size_t i = 0; i < transactionsCount; ++i) {
        if (m_node->getBlockChain().findWalletId(m_spammerAddress, id)) {
          transaction.set_source(csdb::Address::from_wallet_id(id));
        } else {
          transaction.set_source(m_spammerAddress);
        }

        if (m_node->getBlockChain().findWalletId(m_spamKeys[counter], id)) {
          transaction.set_target(csdb::Address::from_wallet_id(id));
        } else {
          transaction.set_target(m_spamKeys[counter]);
        }

        transaction.set_amount(csdb::Amount(randFT(1, 1000), 0));
        transaction.set_max_fee(csdb::AmountCommission(0.1));
        transaction.set_innerID(iid++);

        addConveyerTransaction(transaction);

        if (counter++ == NUM_OF_SPAM_KEYS - 1) {
          counter = 0;
        }
      }
    }

    std::this_thread::sleep_for(std::chrono::microseconds(TRX_SLEEP_TIME));
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

  transaction.set_currency(csdb::Currency(1));
  transaction.set_amount(csdb::Amount(10000, 0));
  transaction.set_innerID(1);

  addConveyerTransaction(transaction);
}

void Solver::runSpammer() {
#ifdef SPAMMER
  m_spamThread = std::thread(&Solver::spamWithTransactions, this);
  m_spamThread.detach();
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

const cs::TransactionsBlock& Solver::transactionsBlock() const
{
  return m_transactionsBlock;
}

const cs::Notifications& Solver::notifications() const {
  return m_notifications;
}

void Solver::addNotification(const cs::Bytes& bytes) {
  csdebug() << "SOLVER> notification added";
  m_notifications.push_back(bytes);
}

std::size_t Solver::neededNotifications() const {
  return (m_roundTable.confidants.size() / 2) + 1;  // TODO: check +1 correctness
}

bool Solver::isEnoughNotifications(NotificationState state) const {
  const std::size_t neededConfidantsCount = neededNotifications();
  const std::size_t notificationsCount = notifications().size();

  cslog() << "SOlVER> Current notifications count - " << notificationsCount;
  cslog() << "SOLVER> Needed confidans count - " << neededConfidantsCount;

  if (state == NotificationState::Equal) {
  return notificationsCount == neededConfidantsCount;
  }
  else {
    return notificationsCount >= neededConfidantsCount;
  }
}

void Solver::addCharacteristicMeta(const CharacteristicMeta& meta) {
  auto iterator = std::find(m_characteristicMeta.begin(), m_characteristicMeta.end(), meta);

  if (iterator != m_characteristicMeta.end()) {
    m_characteristicMeta.push_back(meta);
    csdebug() << "SOLVER> Characteristic meta added";
  }
  else {
    csdebug() << "SOLVER> Received meta is currently in meta stack";
  }
}

CharacteristicMeta Solver::characteristicMeta(const RoundNumber round) {
  cs::CharacteristicMeta meta;
  meta.round = round;

  auto iterator = std::find(m_characteristicMeta.begin(), m_characteristicMeta.end(), meta);

  if (iterator != m_characteristicMeta.end()) {
    meta = std::move(*iterator);
    m_characteristicMeta.erase(iterator);

    return meta;
  }
  else {
    cserror() << "SOLVER> Characteristic meta not found";
    return {};
  }
}

bool Solver::isCharacteristicMetaReceived(const RoundNumber round) {
  cs::CharacteristicMeta meta;
  meta.round = round;

  auto iterator = std::find(m_characteristicMeta.begin(), m_characteristicMeta.end(), meta);
  return iterator != m_characteristicMeta.end();
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
    m_node->getBlockChain().onBlockReceived(pool);
}

void Solver::nextRound() {
  cslog() << "SOLVER> next Round : Starting ... nextRound";

  m_hashesReceivedKeys.clear();

  m_notifications.clear();

  m_blockCandidateArrived = false;
  m_gotBlockThisRound = false;
  m_roundTableSent = false;
  m_isConsensusRunning = false;

  if (m_isPoolClosed) {
    m_vPool = csdb::Pool{};
  }

  if (m_node->getMyLevel() == NodeLevel::Confidant) {
    cs::Utils::clearMemory(m_receivedVectorFrom);
    cs::Utils::clearMemory(m_receivedMatrixFrom);

    trustedCounterVector = 0;
    trustedCounterMatrix = 0;

    cslog() << "SOLVER> next Round : the variables initialized";

#ifdef SPAMMER
    m_isSpamRunning = false;
#endif
  } else {
#ifdef SPAMMER
    m_isSpamRunning = true;
#endif
    m_isPoolClosed = true;

    if (!m_sendingPacketTimer.isRunning()) {
      cslog() << "Transaction timer started";
      m_sendingPacketTimer.start(TransactionsPacketInterval);
    }
  }
}

void Solver::addConveyerTransaction(const csdb::Transaction& transaction) {
  cs::Lock lock(m_sharedMutex);

  if (m_transactionsBlock.empty() || (m_transactionsBlock.back().transactionsCount() >= MaxPacketTransactions)) {
    m_transactionsBlock.push_back(cs::TransactionsPacket());
  }

  m_transactionsBlock.back().addTransaction(transaction);
}

const cs::PrivateKey& Solver::privateKey() const {
  return m_privateKey;
}

const cs::PublicKey& Solver::publicKey() const {
  return m_publicKey;
}

cs::SharedMutex& Solver::sharedMutex() {
  return m_sharedMutex;
}

cs::TransactionsPacket Solver::removeTransactionsWithBadSignatures(const cs::TransactionsPacket& packet)
{
  cs::TransactionsPacket good_pool;
  std::vector<csdb::Transaction> transactions = packet.transactions();
  BlockChain::WalletData data_to_fetch_pulic_key;
  for (int i = 0; i < transactions.size(); ++i) {
    if (transactions[i].source().is_wallet_id()) {
      m_node->getBlockChain().findWalletData(transactions[i].source().wallet_id(), data_to_fetch_pulic_key);
      if (transactions[i].verify_signature(csdb::internal::byte_array(data_to_fetch_pulic_key.address_.begin(),
        data_to_fetch_pulic_key.address_.end()))) {
        good_pool.addTransaction(transactions[i]);
        continue;
      }
    }
    if (transactions[i].verify_signature(transactions[i].source().public_key()))
      good_pool.addTransaction(transactions[i]);
  }
  return good_pool;
}

}  // namespace cs
