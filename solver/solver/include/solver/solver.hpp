////////////////////////////////////////////////////////////////////////////////////////////////////////////
//                    Created by Analytical Solytions Core Team 07.09.2018                                //
////////////////////////////////////////////////////////////////////////////////////////////////////////////
#ifndef SOLVER_HPP
#define SOLVER_HPP

#include <api_types.h>
#include <csdb/csdb.h>
#include <csdb/transaction.h>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <shared_mutex>
#include <string>
#include <thread>
#include <vector>

#include <client/params.hpp>
#include <csnode/nodecore.hpp>
#include <lib/system/keys.hpp>
#include <lib/system/timer.hpp>
#include <solver/fee.hpp>
#include <solver/spammer.hpp>
#include <solver/walletsstate.hpp>

class Node;

namespace slv2 {
class SolverCore;
}

namespace cs {

class Generals;

class Solver {
public:
  explicit Solver(Node*, csdb::Address m_genesisAddress, csdb::Address startAddres);
  ~Solver();

  Solver(const Solver&) = delete;
  Solver& operator=(const Solver&) = delete;

  void setKeysPair(const cs::PublicKey& publicKey, const cs::PrivateKey& privateKey);

  // Solver solves stuff
  void gotTransaction(csdb::Transaction&&);
  void gotRound();
  void gotBlockCandidate(csdb::Pool&&);
  void gotVector(HashVector&&);
  void gotMatrix(HashMatrix&&);
  void gotBlock(csdb::Pool&&, const cs::PublicKey&);
  void gotHash(csdb::PoolHash&&, const cs::PublicKey&);
  void gotBlockRequest(csdb::PoolHash&&);
  void gotBlockReply(csdb::Pool&&);
  void gotBadBlockHandler(csdb::Pool&&, const cs::PublicKey&);
  void gotIncorrectBlock(csdb::Pool&& block);
  void gotFreeSyncroBlock(csdb::Pool&&);
  void rndStorageProcessing();
  void tmpStorageProcessing();
  // restored from cs_dev:
  uint32_t getNextMissingBlock(const uint32_t fromSeq);

  cs::PublicKey writerPublicKey() const;

  uint32_t getTLsize();
  void addInitialBalance();
  void runSpammer();

  void send_wallet_transaction(const csdb::Transaction& transaction);

  void nextRound();
  bool isPoolClosed() const;

  // consesnsus evaluation
  void runConsensus();
  void runFinalConsensus();

  // node interface
  NodeLevel nodeLevel() const;
  const cs::PublicKey& nodePublicKey() const;

  const HashVector& hashVector() const;
  const HashMatrix& hashMatrix() const;

  bool isPoolClosed();

  bool bigBangStatus();
  void setBigBangStatus(bool status);

  const cs::PrivateKey& privateKey() const;
  const cs::PublicKey& publicKey() const;

  void countFeesInPool(csdb::Pool* pool);

  static void addTimestampToPool(csdb::Pool& pool);

  bool checkTransactionSignature(const csdb::Transaction& transaction);

private:
  bool addVector(const HashVector& hashVector);

private:
  cs::PublicKey m_publicKey;
  cs::PrivateKey m_privateKey;

  friend class slv2::SolverCore;

  Node* m_node;
  Spammer m_spammer;

  std::unique_ptr<WalletsState> m_walletsState;
  std::unique_ptr<Generals> m_generals;

  const csdb::Address m_genesisAddress;
  const csdb::Address m_startAddress;

  Fee m_feeCounter;
  HashVector m_hashVector;

  std::vector<uint8_t> m_receivedVectorFrom;
  std::vector<uint8_t> m_receivedMatrixFrom;

  uint8_t m_writerIndex;  // index at confidants

  std::vector<PublicKey> m_hashesReceivedKeys;

  csdb::Pool m_vPool;  // TODO: what is v pool?

  bool m_isPoolClosed = true;
  bool m_blockCandidateArrived = false;
  bool m_gotBlockThisRound = false;

  std::atomic<bool> m_roundTableSent = false;
  std::atomic<bool> m_gotBigBang = false;
  std::atomic<bool> m_isConsensusRunning = false;

  // to store new blocks
  std::map<size_t, csdb::Pool> m_temporaryStorage;

  // to store unrequested syncro blocks
  std::map<size_t, csdb::Pool> m_randomStorage;  // TODO: RND pool or random?
};
}  // namespace cs
#endif
