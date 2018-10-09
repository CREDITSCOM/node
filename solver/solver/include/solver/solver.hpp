////////////////////////////////////////////////////////////////////////////////////////////////////////////
//                    Created by Analytical Solytions Core Team 07.09.2018                                //
////////////////////////////////////////////////////////////////////////////////////////////////////////////
#ifndef SOLVER_HPP
#define SOLVER_HPP

#include <csdb/csdb.h>
#include <memory>
#include <thread>

#include <api_types.h>
#include <functional>

#include <atomic>
#include <functional>

#include <shared_mutex>

#include <set>
#include <string>
#include <thread>
#include <vector>

#include <api_types.h>
#include <csdb/transaction.h>
#include <boost/asio.hpp>

#include <csnode/nodecore.h>
#include <lib/system/timer.hpp>
#include <client/params.hpp>
#include <lib/system/keys.hpp>

class Node;

namespace cs {

class Generals;

class Solver {
 public:
  explicit Solver(Node*);
  ~Solver();

  Solver(const Solver&) = delete;
  Solver& operator=(const Solver&) = delete;

  void set_keys(const std::vector<uint8_t>& pub, const std::vector<uint8_t>& priv);

  // Solver solves stuff
  void gotTransaction(csdb::Transaction&&);
  void gotTransactionsPacket(cs::TransactionsPacket&& packet);
  void gotPacketHashesRequest(std::vector<cs::TransactionsPacketHash>&& hashes, const PublicKey& sender);
  void gotPacketHashesReply(cs::TransactionsPacket&& packet);
  void gotRound(cs::RoundTable&& round);
  void gotBlockCandidate(csdb::Pool&&);
  void gotVector(HashVector&&);
  void gotMatrix(HashMatrix&&);
  void gotBlock(csdb::Pool&&, const PublicKey&);
  void gotHash(std::string&&, const PublicKey&);
  void gotBlockRequest(csdb::PoolHash&&, const PublicKey&);
  void gotBlockReply(csdb::Pool&&);
  void gotBadBlockHandler(csdb::Pool&&, const PublicKey&);
  void gotIncorrectBlock(csdb::Pool&&, const PublicKey&);
  void gotFreeSyncroBlock(csdb::Pool&&);
  void sendTL();
  void rndStorageProcessing();
  void tmpStorageProcessing();
  void applyCharacteristic(const std::vector<uint8_t>& characteristic, const uint32_t bitsCount,
                           const PoolMetaInfo& metaInfoPool, const PublicKey& sender);

  const Characteristic& getCharacteristic() const;
  Hash getCharacteristicHash() const;
  std::vector<uint8_t> sign(std::vector<uint8_t> data);

  PublicKey getWriterPublicKey() const;

  // API methods
  void initApi();
  uint32_t getTLsize();
  void addInitialBalance();

  cs::RoundNumber currentRoundNumber();
  const cs::RoundTable& roundTable() const;

  // conveyer start point
  void addTransaction(const csdb::Transaction& transaction);

  void send_wallet_transaction(const csdb::Transaction& transaction);

  void nextRound();
  bool isPoolClosed() const;
  void setLastRoundTransactionsGot(size_t trNum);

  // remove it!!!
  void buildBlock(csdb::Pool& block);
  void runConsensus();

  HashVector getMyVector() const;
  HashMatrix getMyMatrix() const;

  void initConfRound();
  void checkVectorsReceived(size_t _rNum) const;
  void checkMatrixReceived();
  void addConfirmation(uint8_t confNumber_);
  bool getIPoolClosed();
  bool getBigBangStatus();
  void setBigBangStatus(bool _status);
  void setRNum(size_t _rNum);

  const std::vector<uint8_t>& getPrivateKey() const;

 private:
  void _initApi();

  void runMainRound();
  void closeMainRound();

  void flushTransactions();

  void writeNewBlock();
  void prepareBlockForSend(csdb::Pool& block);

#ifdef SPAM_MAIN
  void createPool();

  std::atomic_bool createSpam;
  std::thread      spamThread;

  csdb::Pool testPool;
#endif  // SPAM_MAIN

  // TODO: fix signature
  bool verifySignature(uint8_t signature[64], uint8_t public_key[32], uint8_t* message, size_t message_len);

  std::vector<uint8_t> myPublicKey;
  std::vector<uint8_t> myPrivateKey;

  Node* m_node;

  std::unique_ptr<Generals> m_generals;

  HashVector hvector;

  cs::Hashes m_neededHashes;

  size_t lastRoundTransactionsGot;
  std::set<PublicKey> receivedVec_ips;
  bool receivedVecFrom[100];
  uint8_t trustedCounterVector;

  std::set<PublicKey> receivedMat_ips;

  bool    receivedMatFrom[100];
  uint8_t trustedCounterMatrix;
  uint8_t m_writerIndex; // index at confidants

  std::vector<PublicKey> ips;
  std::vector<std::string> vector_datas;

  csdb::Pool m_pool;
  csdb::Pool m_uncharacterizedPool;

  cs::RoundTable m_roundTable;

  csdb::Pool v_pool;
  csdb::Pool b_pool;

  bool m_isPoolClosed         = true;
  bool sentTransLastRound      = false;
  bool vectorComplete          = false;
  bool consensusAchieved       = false;
  bool blockCandidateArrived   = false;
  bool round_table_sent        = false;
  bool transactionListReceived = false;
  bool vectorReceived          = false;
  bool gotBlockThisRound       = false;
  bool writingConfirmationGot  = false;
  bool gotBigBang              = false;
  bool writingConfGotFrom[100];

  uint8_t writingCongGotCurrent;
  uint32_t rNum = 0;

  cs::SharedMutex m_sharedMutex;

  cs::TransactionsPacketHashTable m_hashTable;
  cs::TransactionsBlock m_transactionsBlock;

  cs::Timer m_sendingPacketTimer;

  /*to store new blocks*/
  std::map<size_t, csdb::Pool> tmpStorage;

  /*to store unrequested syncro blocks*/
  std::map<size_t, csdb::Pool> rndStorage;

  // TODO! Hash m_characteristicHash

#ifdef SPAMMER
  std::atomic_bool spamRunning{false};
  std::thread      spamThread;
  void             spamWithTransactions();
#endif
};
}  // namespace cs
#endif
