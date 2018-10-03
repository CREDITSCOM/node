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
struct Hash_ {
  Hash_(uint8_t* a) {
    memcpy(val, a, 32);
  }
  Hash_() {
  }
  uint8_t val[32];
};
struct Signature {
  Signature(void* a) {
    memcpy(val, a, 64);
  }
  Signature() {
  }
  uint8_t val[64];
};
#pragma pack(push, 1)
struct HashVector {
  uint8_t   Sender;
  Hash_     hash;
  Signature sig;
};
struct HashMatrix {
  uint8_t    Sender;
  HashVector hmatr[5];
  Signature  sig;
};
struct NormalState {
  bool isOn;
  bool rtStartReceived;
  bool transactionSend;
  bool newBlockReceived;
  bool hashSent;
};
struct MainState {
  bool isOn;
  bool rtStartReceived;
  bool transactinReceived;
  bool newBlockReceived;
  bool rtFinishReceived;
  bool tlSent;
};
struct TrustedState {
  bool isOn;
  bool rtStartReceived;
  bool tlReceived;
  bool vectorSent;
  bool allVectorsReceived;
  bool matrixSent;
  bool allMatricesReceived;
  bool writerConfirmationSent;
  bool newBlockReceived;
  bool hashSent;
};
struct WriterState {
  bool isOn;
  bool writerConfirmationReceived;
  bool newBlockSent;
  bool hashesReceived;
  bool trSent;
};

struct SolverStates {
  NormalState  normal;
  MainState    main;
  TrustedState trusted;
  WriterState  writer;
};

#pragma pack(pop)
class State {};

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
  void gotRound(cs::RoundInfo&& round);
  // void gotTransactionList(csdb::Pool&&);
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
                           const csdb::Pool& metaInfoPool, const PublicKey& sender);

  Hash getCharacteristicHash() const;
  std::vector<uint8_t> getSignedNotification();

  PublicKey getWriterPublicKey() const;

  const char* getSignature();

  // API methods
  void     initApi();
  uint32_t getTLsize();
  void     addInitialBalance();

  cs::RoundNumber currentRoundNumber();
  void            addTransaction(const csdb::Transaction& transaction);

  void send_wallet_transaction(const csdb::Transaction& transaction);

  void nextRound();
  bool isPoolClosed();
  void setLastRoundTransactionsGot(size_t trNum);

  // remove it!!!
  void buildBlock(csdb::Pool& block);
  void buildTransactionList();

  HashVector getMyVector() const;
  HashMatrix getMyMatrix() const;

  void initConfRound();
  void sendZeroVector();
  void checkVectorsReceived(size_t _rNum);
  void checkMatrixReceived();
  void addConfirmation(uint8_t confNumber_);
  bool getIPoolClosed();
  bool getBigBangStatus();
  void setBigBangStatus(bool _status);
  void setRNum(size_t _rNum);
  void setConfidants(const std::vector<PublicKey>& confidants, const PublicKey& general, const RoundNumber roundNum);

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

  bool verify_signature(uint8_t signature[64], uint8_t public_key[32], uint8_t* message, size_t message_len);

  std::vector<uint8_t> myPublicKey;
  std::vector<uint8_t> myPrivateKey;

  Node* node_;

  std::unique_ptr<Generals> generals;

  HashVector hvector;

  cs::Hashes m_neededHashes;

  size_t              lastRoundTransactionsGot;
  std::set<PublicKey> receivedVec_ips;
  bool                receivedVecFrom[100];
  uint8_t             trustedCounterVector;

  std::set<PublicKey> receivedMat_ips;

  bool    receivedMatFrom[100];
  uint8_t trustedCounterMatrix;
  uint8_t m_writerIndex; // index at confidants

  std::vector<PublicKey> ips;
  std::vector<std::string> vector_datas;

  csdb::Pool m_pool;
  csdb::Pool m_uncharacterizedPool;

  cs::RoundInfo m_roundInfo;

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
  uint32_t  rNum = 0;

  cs::SharedMutex mSharedMutex;

  cs::TransactionsPacketHashTable m_hashTable;
  cs::TransactionsBlock           m_transactionsBlock;

  cs::Timer m_sendingPacketTimer;

  /*to store new blocks*/
  std::map<size_t, csdb::Pool> tmpStorage;
  /*to store unrequested syncro blocks*/
  std::map<size_t, csdb::Pool> rndStorage;

#ifdef SPAMMER
  std::atomic_bool spamRunning{false};
  std::thread      spamThread;
  void             spamWithTransactions();
#endif
};
}  // namespace cs
#endif
