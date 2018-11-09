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
#include <csnode/conveyer.hpp>
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
  m_receivedVectorFrom.reserve(cs::hashVectorCount); // TODO Maybe = 100. Is max trusted count
  m_receivedMatrixFrom.reserve(cs::hashVectorCount); // TODO Maybe = 100. Is max trusted count
}

Solver::~Solver() {
}

void Solver::setKeysPair(const cs::PublicKey& publicKey, const cs::PrivateKey& privateKey) {
  m_publicKey = publicKey;
  m_privateKey = privateKey;
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

  if (m_node->getNodeLevel() == NodeLevel::Confidant) {
    cs::SharedLock lock(cs::Conveyer::instance().sharedMutex());
    runConsensus();
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

  m_feeCounter.CountFeesInPool(m_node->getBlockChain(), &packet);

  cs::Hash result = m_generals->buildVector(packet, this);

  m_hashVector.sender = m_node->getConfidantNumber();
  m_hashVector.hash = result;

  m_node->sendVector(m_hashVector);

  addVector(m_hashVector);
}

void Solver::runFinalConsensus() {
  const auto& confidants = cs::Conveyer::instance().roundTable().confidants;

  if (m_receivedMatrixFrom.size() == confidants.size()) {
    const auto& hash = m_node->getBlockChain().getHashBySequence(m_node->getRoundNumber() - 1);
    m_writerIndex = m_generals->takeDecision(confidants, hash);

    if (m_writerIndex == 100) {
      cserror() << "SOLVER> CONSENSUS WASN'T ACHIEVED!!!";
    }
    else {
      cslog() << "SOLVER> CONSENSUS ACHIEVED!!!";
      cslog() << "SOLVER> m_writerIndex = " << static_cast<int>(m_writerIndex);

      if (m_writerIndex == m_node->getConfidantNumber()) {
        m_node->becomeWriter();
      }
      else {
        m_node->sendWriterNotification();
      }
    }
  }
}

NodeLevel Solver::nodeLevel() const {
  return m_node->getNodeLevel();
}

const PublicKey& Solver::nodePublicKey() const {
  return m_node->getNodeIdKey();
}

void Solver::gotVector(HashVector&& vector) {
  cslog() << "SOLVER> GotVector";

  const auto alreadyGot = std::find(m_receivedVectorFrom.begin(), m_receivedVectorFrom.end(), vector.sender);

  if (alreadyGot != m_receivedVectorFrom.end()) {
    cslog() << "SOLVER> I've already got the vector from this Node";
    return;
  }

  if (addVector(vector)) {
    runFinalConsensus();
  }

  cslog() << "Solver>  VECTOR GOT SUCCESSFULLY!!!";
}

void Solver::gotMatrix(HashMatrix&& matrix) {
  if (m_gotBlockThisRound) {
    return;
  }

  const auto alreadyGot = std::find(m_receivedMatrixFrom.begin(), m_receivedMatrixFrom.end(), matrix.sender);

  if (alreadyGot != m_receivedMatrixFrom.end()) {
    cslog() << "SOLVER> I've already got the matrix from this Node";
    return;
  }

  m_receivedMatrixFrom.push_back(matrix.sender);
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
        auto hash = m_node->getBlockChain().getLastWrittenHash();
        m_node->sendHash(hash, sender);
        csdebug() << "SENDING HASH: " << hash.to_string();
      }
#endif
    }
  }
}

void Solver::gotIncorrectBlock(csdb::Pool&& block, const PublicKey& sender) {
  cslog() << __func__;

  if (m_temporaryStorage.count(block.sequence()) == 0) {
    m_temporaryStorage.emplace(block.sequence(), block);
    cslog() << "GOT INCORRECT BLOCK> block saved to temporary storage: " << block.sequence();
  }
}

void Solver::gotFreeSyncroBlock(csdb::Pool&& block) {
  cslog() << __func__;

  if (m_randomStorage.count(block.sequence()) == 0) {
    m_randomStorage.emplace(block.sequence(), block);
    cslog() << "GOT FREE SYNCRO BLOCK> block saved to temporary storage: " << block.sequence();
  }
}

void Solver::rndStorageProcessing() {
  cslog() << __func__;
  bool loop = true;
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

void Solver::gotHash(csdb::PoolHash&& hash, const PublicKey& sender) {
  if (m_roundTableSent) {
    return;
  }

  csdb::PoolHash myHash = m_node->getBlockChain().getLastWrittenHash();

  cslog() << "Solver -> My Hash: " << myHash.to_string();
  cslog() << "Solver -> Received hash:" << hash.to_string();

  cslog() << "Solver -> Received public key: " << cs::Utils::byteStreamToHex(sender.data(), sender.size());

  if (m_hashesReceivedKeys.size() <= min_nodes) {
    if (hash == myHash) {
      csdebug() << "Solver -> Hashes are good";
      auto iterator = std::find(m_hashesReceivedKeys.begin(), m_hashesReceivedKeys.end(), sender);

      if (iterator == m_hashesReceivedKeys.end()) {
        cslog() << "Solver - > Add sender to next confidant list";
        m_hashesReceivedKeys.push_back(sender);
      }
    }
    else {
      cslog() << "Hashes do not match!!!";
      return;
    }
  }
  else {
    cslog() << "Solver -> We have enough hashes!";
    return;
  }

  csdebug() << "Got " << m_hashesReceivedKeys.size() << " of " << min_nodes << " nodes to start new round";
  
  if ((m_hashesReceivedKeys.size() == min_nodes) && (!m_roundTableSent)) {
    cslog() << "Solver -> sending NEW ROUND table";

    cs::Hashes hashes;
    cs::Conveyer& conveyer = cs::Conveyer::instance();
    cs::RoundNumber round = conveyer.currentRoundNumber();

    {
      cs::SharedLock lock(conveyer.sharedMutex());

      for (const auto& element : conveyer.transactionsPacketTable()) {
        hashes.push_back(element.first);
      }
    }

    cs::RoundTable table;
    table.round = ++round;
    table.confidants = std::move(m_hashesReceivedKeys);
    table.general = m_node->getNodeIdKey();
    table.hashes = std::move(hashes);

    conveyer.setRound(std::move(table));

    m_hashesReceivedKeys.clear();

    cslog() << "Solver -> NEW ROUND initialization done";

    if (!m_roundTableSent) {
      m_node->initNextRound(cs::Conveyer::instance().roundTable());
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
    m_receivedVectorFrom.clear();
    m_receivedMatrixFrom.clear();
    m_generals->resetHashMatrix();

    cslog() << "SOLVER> next Round : the variables initialized";
  }
  else {
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
  m_feeCounter.CountFeesInPool(m_node->getBlockChain(), pool);
}

void Solver::addTimestampToPool(csdb::Pool& pool) {
  const auto now_time = std::chrono::system_clock::now();
  const auto count = std::chrono::duration_cast<std::chrono::milliseconds>(now_time.time_since_epoch()).count();

  pool.add_user_field(0, std::to_string(count));
}

bool Solver::checkTransactionSignature(const csdb::Transaction& transaction)
{
  BlockChain::WalletData data_to_fetch_pulic_key;

  if (transaction.source().is_wallet_id()) {
    m_node->getBlockChain().findWalletData(transaction.source().wallet_id(), data_to_fetch_pulic_key);

    csdb::internal::byte_array byte_array(data_to_fetch_pulic_key.address_.begin(),
                                          data_to_fetch_pulic_key.address_.end());
    return transaction.verify_signature(byte_array);
  }

  return transaction.verify_signature(transaction.source().public_key());
}

bool Solver::addVector(const HashVector& hashVector) {
  m_receivedVectorFrom.push_back(hashVector.sender);
  m_generals->addVector(hashVector);

  const auto& confidants = cs::Conveyer::instance().roundTable().confidants;
  const auto confidantsSize = confidants.size();
  const auto receivedVectorsSize = m_receivedVectorFrom.size();

  csdebug() << "SOLVER> Trusted counter vector: " << receivedVectorsSize;
  csdebug() << "SOVLER> Confidants size: " << confidantsSize;

  if (receivedVectorsSize != confidantsSize) {
    return false;
  }

  // compose and send matrix!!!
  const uint8_t confNumber = m_node->getConfidantNumber();
  m_generals->addSenderToMatrix(confNumber);
  m_receivedMatrixFrom.push_back(confNumber);

  const HashMatrix& matrix = hashMatrix();
  m_node->sendMatrix(matrix);
  m_generals->addMatrix(matrix, confidants);  // MATRIX SHOULD BE DECOMPOSED HERE!!!

  cslog() << "SOLVER> Matrix added";

  return true;
}

}  // namespace cs
