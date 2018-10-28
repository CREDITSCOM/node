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
#include <csnode/conveyer.h>
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
, m_spammer()
, m_walletsState(new WalletsState(node->getBlockChain()))
, m_generals(std::unique_ptr<Generals>(new Generals(*m_walletsState)))
, m_genesisAddress(_genesisAddress)
, m_startAddress(_startAddress)
#ifdef SPAMMER
, m_spammerAddress(_spammerAddress)
#endif
, m_feeCounter()
, m_writerIndex(0) {
}

Solver::~Solver() {
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

cs::PublicKey Solver::writerPublicKey() const {
  PublicKey result;
  const cs::ConfidantsKeys& confidants = cs::Conveyer::instance().roundTable().confidants;

  if (m_writerIndex < confidants.size()) {
    result = confidants[m_writerIndex];
  }
  else {
    cserror() << "WRITER PUBLIC KEY IS NOT EXIST AT CONFIDANTS. LOGIC ERROR!";
  }

  return result;
}

bool Solver::isPoolClosed() const {
  return m_isPoolClosed;
}

const HashVector& Solver::hashVector() const {
  return m_hashVector;
}

const HashMatrix& Solver::hashMatrix() const {
  return (m_generals->getMatrix());
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

void Solver::gotRound() {
  if (!cs::Conveyer::instance().isSyncCompleted()) {
    csdebug() << "SOLVER> Packets sync is not completed";
    return;
  }

  cslog() << "SOLVER> Got round";

  nextRound();

  if (m_node->getNodeLevel() == NodeLevel::Confidant) {
    cs::Timer::singleShot(TIME_TO_AWAIT_ACTIVITY, [this] {
      runConsensus();
    });
  }
}

void Solver::runConsensus() {
  if (m_isConsensusRunning) {
    return;
  }

  m_isConsensusRunning = true;

  cslog() << "SOLVER> Run Consensus";
  cs::TransactionsPacket packet;
  cs::Conveyer& conveyer = cs::Conveyer::instance();

  for (const auto& hash : conveyer.roundTable().hashes) {
    const auto& hashTable = conveyer.transactionsPacketTable();

    if (!hashTable.count(hash)) {
      cserror() << "Consensus build vector: HASH NOT FOUND";
      return;
    }

    const auto& transactions = conveyer.packet(hash).transactions();

    for (const auto& transaction : transactions) {
      if (!packet.addTransaction(transaction)) {
        cserror() << "Can not add transaction to packet in consensus";
      }
    }
  }

  cslog() << "SOLVER> Consensus transaction packet of " << packet.transactionsCount() << " transactions";

  packet = removeTransactionsWithBadSignatures(packet);

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

  m_receivedVectorFrom[m_node->getConfidantNumber()] = true;

  m_hashVector.sender = m_node->getConfidantNumber();
  m_hashVector.hash = result;

  m_receivedVectorFrom[m_node->getConfidantNumber()] = true;

  m_generals->addVector(m_hashVector);
  m_node->sendVector(m_hashVector);

  trustedCounterVector++;

  if (trustedCounterVector == conveyer.roundTable().confidants.size()) {
    std::memset(m_receivedVectorFrom, 0, sizeof(m_receivedVectorFrom));
    trustedCounterVector = 0;

    // compose and send matrix!!!
    m_generals->addSenderToMatrix(m_node->getConfidantNumber());

    m_receivedMatrixFrom[m_node->getConfidantNumber()] = true;
    ++trustedCounterMatrix;

    m_node->sendMatrix(m_generals->getMatrix());
    m_generals->addMatrix(m_generals->getMatrix(), conveyer.roundTable().confidants);  // MATRIX SHOULD BE DECOMPOSED HERE!!!

    cslog() << "SOLVER> Matrix added";
  }
}

void Solver::runFinalConsensus() {
  const cs::RoundTable& table = cs::Conveyer::instance().roundTable();
  const uint8_t numGen = static_cast<uint8_t>(table.confidants.size());

  if (trustedCounterMatrix == numGen) {
    std::memset(m_receivedMatrixFrom, 0, sizeof(m_receivedMatrixFrom));

    m_writerIndex = (m_generals->takeDecision(table.confidants,
                                              m_node->getBlockChain().getHashBySequence(m_node->getRoundNumber() - 1)));
    trustedCounterMatrix = 0;

    if (m_writerIndex == 100) {
      cslog() << "SOLVER> CONSENSUS WASN'T ACHIEVED!!!";
    }
    else {
      cslog() << "SOLVER> CONSENSUS ACHIEVED!!!";
      cslog() << "SOLVER> m_writerIndex = " << static_cast<int>(m_writerIndex);

      if (m_writerIndex == m_node->getConfidantNumber()) {
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

NodeLevel Solver::nodeLevel() const {
  return m_node->getNodeLevel();
}

const PublicKey& Solver::nodePublicKey() const {
  return m_node->getPublicKey();
}

void Solver::gotVector(HashVector&& vector) {
  cslog() << "SOLVER> GotVector";

  if (m_receivedVectorFrom[vector.sender] == true) {
    cslog() << "SOLVER> I've already got the vector from this Node";
    return;
  }

  const cs::ConfidantsKeys& confidants = cs::Conveyer::instance().roundTable().confidants;
  uint8_t numGen = static_cast<uint8_t>(confidants.size());

  m_receivedVectorFrom[vector.sender] = true;

  m_generals->addVector(vector);  // building matrix
  trustedCounterVector++;

  if (trustedCounterVector == numGen) {
    std::memset(m_receivedVectorFrom, 0, sizeof(m_receivedVectorFrom));
    trustedCounterVector = 0;

    // compose and send matrix!!!
    uint8_t confNumber = m_node->getConfidantNumber();
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
  m_generals->addMatrix(matrix, cs::Conveyer::instance().roundTable().confidants);

  runFinalConsensus();
}

void Solver::gotBlock(csdb::Pool&& block, const PublicKey& sender) {
  if (m_node->getNodeLevel() == NodeLevel::Writer) {
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
      if ((m_node->getNodeLevel() != NodeLevel::Writer) && (m_node->getNodeLevel() != NodeLevel::Main)) {
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

  csdebug() << "Got " << m_hashesReceivedKeys.size() << " of " << min_nodes << " nodes to start new round";
  
  if ((m_hashesReceivedKeys.size() == min_nodes) && (!m_roundTableSent)) {
    cslog() << "Solver -> sending NEW ROUND table";

    cs::Hashes hashes;
    cs::Conveyer& conveyer = cs::Conveyer::instance();
    cs::RoundNumber round = conveyer.roundNumber();

    {
      cs::SharedLock lock(conveyer.sharedMutex());
      for (const auto& element : conveyer.transactionsPacketTable()) {
        hashes.push_back(element.first);
      }
    }

    cs::RoundTable table;
    table.round = ++round;
    table.confidants = std::move(m_hashesReceivedKeys);
    table.general = m_node->getPublicKey();
    table.hashes = std::move(hashes);

    conveyer.setRound(std::move(table));

    m_hashesReceivedKeys.clear();

    cslog() << "Solver -> NEW ROUND initialization done";

    if (!m_roundTableSent) {
      cs::Timer::singleShot(ROUND_DELAY, [=]() {
        m_node->initNextRound(cs::Conveyer::instance().roundTable());
      });

      m_roundTableSent = true;
    }
  }
}

void Solver::send_wallet_transaction(const csdb::Transaction& transaction) {
  cs::Conveyer::instance().addTransaction(transaction);
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

  cs::Conveyer::instance().addTransaction(transaction);
}

void Solver::runSpammer() {
#ifdef SPAMMER
  m_spammer.StartSpamming(*m_node);
#endif
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

  m_blockCandidateArrived = false;
  m_gotBlockThisRound = false;
  m_roundTableSent = false;
  m_isConsensusRunning = false;

  if (m_isPoolClosed) {
    m_vPool = csdb::Pool{};
  }

  if (m_node->getNodeLevel() == NodeLevel::Confidant) {
    cs::Utils::clearMemory(m_receivedVectorFrom);
    cs::Utils::clearMemory(m_receivedMatrixFrom);

    trustedCounterVector = 0;
    trustedCounterMatrix = 0;

    cslog() << "SOLVER> next Round : the variables initialized";
  } else {
    m_isPoolClosed = true;
  }
}

const cs::PrivateKey& Solver::privateKey() const {
  return m_privateKey;
}

const cs::PublicKey& Solver::publicKey() const {
  return m_publicKey;
}

void Solver::countFeesInPool(csdb::Pool* pool) {
  m_feeCounter.CountFeesInPool(m_node, pool);
}

void Solver::addTimestampToPool(csdb::Pool& pool) {
  const auto now_time = std::chrono::system_clock::now();
  const auto count = std::chrono::duration_cast<std::chrono::milliseconds>(now_time.time_since_epoch()).count();

  pool.add_user_field(0, std::to_string(count));
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
