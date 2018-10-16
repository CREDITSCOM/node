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

  void setKeysPair(const cs::PublicKey& publicKey, const cs::PrivateKey& privateKey);

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
  boost::optional<csdb::Pool> applyCharacteristic(const cs::Characteristic& characteristic,
                           const PoolMetaInfo& metaInfoPool, const PublicKey& sender = cs::PublicKey());

  const Characteristic& getCharacteristic() const;
  Hash getCharacteristicHash() const;

  PublicKey getWriterPublicKey() const;

  uint32_t getTLsize();
  void addInitialBalance();
  void runSpammer();

  cs::RoundNumber currentRoundNumber();
  const cs::RoundTable& roundTable() const;
  const cs::TransactionsPacketHashTable& transactionsPacketTable() const;

  // notifications interface
  const cs::Notifications& notifications() const;
  void addNotification(const cs::Bytes& bytes);
  std::size_t neededNotifications() const;
  bool isEnoughNotifications() const;

  // conveyer start point
  void addConveyerTransaction(const csdb::Transaction& transaction);

  void send_wallet_transaction(const csdb::Transaction& transaction);

  void nextRound();
  bool isPoolClosed() const;

  // consesnsus evaluation
  void runConsensus();
  void runFinalConsensus();

  // helpers
  void removePreviousHashes();
  bool checkTableHashes(const cs::RoundTable& table);

  HashVector getMyVector() const;
  HashMatrix getMyMatrix() const;

  bool getIPoolClosed();
  bool getBigBangStatus();
  void setBigBangStatus(bool _status);

  const cs::PrivateKey& getPrivateKey() const;
  const cs::PublicKey& getPublicKey() const;

 private:
  void flushTransactions();

  // TODO: fix signature
  bool verifySignature(uint8_t signature[64], uint8_t public_key[32], uint8_t* message, size_t message_len);

  cs::PublicKey myPublicKey;
  cs::PrivateKey myPrivateKey;

  Node* m_node;

  std::unique_ptr<Generals> m_generals;

  HashVector hvector;

  cs::Hashes m_neededHashes;

  bool receivedVecFrom[100];
  std::atomic<uint8_t> trustedCounterVector;

  bool receivedMatFrom[100];
  std::atomic<uint8_t> trustedCounterMatrix;
  uint8_t m_writerIndex; // index at confidants

  std::vector<PublicKey> ips;

  cs::RoundTable m_roundTable;

  csdb::Pool v_pool;

  bool m_isPoolClosed = true;
  bool blockCandidateArrived = false;
  bool round_table_sent = false;
  bool gotBlockThisRound = false;
  bool gotBigBang = false;
  std::atomic<bool> isConsensusRunning = { false };

  cs::SharedMutex m_sharedMutex;

  cs::TransactionsPacketHashTable m_hashTable;
  cs::TransactionsBlock m_transactionsBlock;
  cs::Notifications m_notifications;
  cs::HashesSet m_hashesToRemove;

  cs::Timer m_sendingPacketTimer;

  /*to store new blocks*/
  std::map<size_t, csdb::Pool> tmpStorage;

  /*to store unrequested syncro blocks*/
  std::map<size_t, csdb::Pool> rndStorage;

  // TODO! Hash m_characteristicHash

#ifdef SPAMMER
  std::atomic_bool spamRunning = { false };
  std::thread spamThread;
  void spamWithTransactions();
#endif
};
}  // namespace cs
#endif
