////////////////////////////////////////////////////////////////////////////////////////////////////////////
//                    Created by Analytical Solytions Core Team 07.09.2018                                //
////////////////////////////////////////////////////////////////////////////////////////////////////////////
#ifndef SOLVER_HPP
#define SOLVER_HPP

#include <csdb/csdb.h>
#include <csdb/transaction.h>
#include <api_types.h>

#include <memory>
#include <thread>
#include <functional>
#include <atomic>
#include <shared_mutex>
#include <set>
#include <string>
#include <vector>
#include <optional>

#include <csnode/nodecore.h>
#include <lib/system/timer.hpp>
#include <client/params.hpp>
#include <lib/system/keys.hpp>
#include <solver/WalletsState.h>
#include <solver/Fee.h>

class Node;

namespace slv2
{
  class SolverCore;
}

namespace cs {

class Generals;

class Solver {
public:
   enum class NotificationState {
     Equal,
     GreaterEqual
   };

   enum : unsigned int {
     HashTablesStorageCapacity = 10
   };

  explicit Solver(Node*, csdb::Address m_genesisAddress, csdb::Address startAddres
#ifdef SPAMMER
  , csdb::Address m_spammerAddress
#endif
  );
  ~Solver();

  Solver(const Solver&) = delete;
  Solver& operator=(const Solver&) = delete;

  void setKeysPair(const cs::PublicKey& publicKey, const cs::PrivateKey& privateKey);

  // Solver solves stuff
  void gotTransaction(csdb::Transaction&&);
  void gotTransactionsPacket(cs::TransactionsPacket&& packet);
  void gotPacketHashesRequest(cs::Hashes&& hashes, const cs::RoundNumber round, const cs::PublicKey& sender);
  void gotPacketHashesReply(cs::TransactionsPacket&& packet);
  void gotRound(cs::RoundTable&& round);
  void gotBlockCandidate(csdb::Pool&&);
  void gotVector(HashVector&&);
  void gotMatrix(HashMatrix&&);
  void gotBlock(csdb::Pool&&, const cs::PublicKey&);
  void gotHash(std::string&&, const cs::PublicKey&);
  void gotBlockRequest(csdb::PoolHash&&, const cs::PublicKey&);
  void gotBlockReply(csdb::Pool&&);
  void gotBadBlockHandler(csdb::Pool&&, const cs::PublicKey&);
  void gotIncorrectBlock(csdb::Pool&&, const cs::PublicKey&);
  void gotFreeSyncroBlock(csdb::Pool&&);
  void sendTL();
  void rndStorageProcessing();
  void tmpStorageProcessing();

  std::optional<csdb::Pool> applyCharacteristic(const cs::Characteristic& characteristic,
    const PoolMetaInfo& metaInfoPool, const PublicKey& sender = cs::PublicKey());

  const Characteristic& getCharacteristic() const;
  Hash getCharacteristicHash() const;

  cs::PublicKey writerPublicKey() const;

  uint32_t getTLsize();
  void addInitialBalance();
  void runSpammer();

  cs::RoundNumber currentRoundNumber();
  const cs::RoundTable& roundTable() const;
  const cs::TransactionsPacketHashTable& transactionsPacketTable() const;
  const cs::TransactionsBlock& transactionsBlock() const;

  // notifications interface
  const cs::Notifications& notifications() const;
  void addNotification(const cs::Bytes& bytes);
  std::size_t neededNotifications() const;
  bool isEnoughNotifications(NotificationState state) const;

  // characteristic meta
  void addCharacteristicMeta(const cs::CharacteristicMeta& meta);
  cs::CharacteristicMeta characteristicMeta(const RoundNumber round);
  bool isCharacteristicMetaReceived(const RoundNumber round);

  // conveyer start point
  void addConveyerTransaction(const csdb::Transaction& transaction);
  void send_wallet_transaction(const csdb::Transaction& transaction);

  void nextRound();
  bool isPoolClosed() const;

  // consesnsus evaluation
  void runConsensus();
  void runFinalConsensus();

  // helpers
  bool checkTableHashes(const cs::RoundTable& table);
  bool isPacketSyncFinished() const;

  HashVector hashVector() const;
  HashMatrix hashMatrix() const;

  bool isPoolClosed();

  bool bigBangStatus();
  void setBigBangStatus(bool status);

  const cs::PrivateKey& privateKey() const;
  const cs::PublicKey& publicKey() const;

  cs::SharedMutex& sharedMutex();

private:
  void flushTransactions();
  cs::TransactionsPacket removeTransactionsWithBadSignatures(const cs::TransactionsPacket& packet);

  cs::PublicKey m_publicKey;
  cs::PrivateKey m_privateKey;

  friend class slv2::SolverCore;

  Node* m_node;

  std::unique_ptr<WalletsState> m_walletsState;
  std::unique_ptr<Generals> m_generals;

  const csdb::Address m_genesisAddress;
  const csdb::Address m_startAddress;

#ifdef SPAMMER
  const csdb::Address m_spammerAddress;
  std::vector<csdb::Address> m_spamKeys;
#endif

  Fee m_feeCounter;
  HashVector m_hashVector;

  bool m_receivedVectorFrom[100];
  std::atomic<uint8_t> trustedCounterVector;

  bool m_receivedMatrixFrom[100];
  std::atomic<uint8_t> trustedCounterMatrix;

  uint8_t m_writerIndex; // index at confidants

  std::vector<PublicKey> m_hashesReceivedKeys;
  cs::RoundTable m_roundTable;

  csdb::Pool m_vPool;   // TODO: what is v pool?

  bool m_isPoolClosed = true;
  bool m_blockCandidateArrived = false;
  bool m_gotBlockThisRound = false;

  std::atomic<bool> m_roundTableSent = false;
  std::atomic<bool> m_gotBigBang = false;
  std::atomic<bool> m_isConsensusRunning = false;

  cs::SharedMutex m_sharedMutex;

  cs::TransactionsPacketHashTable m_hashTable;
  cs::TransactionsBlock m_transactionsBlock;
  cs::Notifications m_notifications;

  cs::HashTablesStorage m_hashTablesStorage;
  cs::Hashes m_neededHashes;
  std::vector<cs::CharacteristicMeta> m_characteristicMeta;

  cs::Timer m_sendingPacketTimer;

  // to store new blocks
  std::map<size_t, csdb::Pool> m_temporaryStorage;

  // to store unrequested syncro blocks
  std::map<size_t, csdb::Pool> m_randomStorage; // TODO: RND pool or random?

#ifdef SPAMMER
  std::atomic_bool m_isSpamRunning = false;
  std::thread m_spamThread;
  void spamWithTransactions();
#endif
};
}  // namespace cs
#endif
