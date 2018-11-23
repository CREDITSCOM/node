#ifndef NODE_HPP
#define NODE_HPP

#include <iostream>
#include <memory>
#include <string>

#include <csconnector/csconnector.hpp>
#include <csstats.hpp>
#include <client/config.hpp>

#include <csnode/conveyer.hpp>
#include <lib/system/keys.hpp>
#include <lib/system/timer.hpp>

#include <net/neighbourhood.hpp>

#include "blockchain.hpp"
#include "packstream.hpp"

class Transport;

namespace slv2 {
class SolverCore;
}

namespace cs {
class PoolSynchronizer;
}

class Node {
public:
  explicit Node(const Config&);
  ~Node();

  bool isGood() const {
    return good_;
  }

  void run();
  void stop();

  // static void stop();

  // incoming requests processing
  void getRoundTableSS(const uint8_t*, const size_t, const cs::RoundNumber, uint8_t type = 0);
  void getVector(const uint8_t*, const size_t, const cs::PublicKey& sender);
  void getMatrix(const uint8_t*, const size_t, const cs::PublicKey& sender);
  void getHash(const uint8_t*, const size_t, const cs::PublicKey& sender);
  void getTransactionsPacket(const uint8_t*, const std::size_t);

  // SOLVER3 methods
  void getStageOne(const uint8_t*, const size_t, const cs::PublicKey& sender);
  void getStageTwo(const uint8_t*, const size_t, const cs::PublicKey& sender);
  void getStageThree(const uint8_t*, const size_t, const cs::PublicKey& sender);
  void getRoundInfo(const uint8_t*, const size_t, const cs::RoundNumber, const cs::PublicKey& sender);

  // SOLVER3 methods
  void sendStageOne(cs::StageOne&);

  // sends StageOne request to respondent about required
  void getHash_V3(const uint8_t* data, const size_t size, cs::RoundNumber rNum, const cs::PublicKey& sender);
  void requestStageOne(uint8_t respondent, uint8_t required);
  void getStageOneRequest(const uint8_t* data, const size_t size, const cs::PublicKey& requester);
  void sendStageOneReply(const cs::StageOne& stageOneInfo, const uint8_t requester);

  void sendStageTwo(const cs::StageTwo&);
  void requestStageTwo(uint8_t respondent, uint8_t required);
  void getStageTwoRequest(const uint8_t* data, const size_t size, const cs::PublicKey& requester);
  void sendStageTwoReply(const cs::StageTwo& stageTwoInfo, const uint8_t requester);

  void sendStageThree(const cs::StageThree&);
  void requestStageThree(uint8_t respondent, uint8_t required);
  void getStageThreeRequest(const uint8_t* data, const size_t size, const cs::PublicKey& requester);
  void sendStageThreeReply(const cs::StageThree& stageThreeInfo, const uint8_t requester);

  void sendHash_V3(cs::RoundNumber round);

  const cs::ConfidantsKeys& confidants() const;

  void onRoundStart_V3(const cs::RoundTable& roundTable);
  void startConsensus();

  void passBlockToSolver(csdb::Pool& pool, const cs::PublicKey& sender);

  void sendRoundInfo(cs::RoundTable& roundTable, cs::PoolMetaInfo poolMetaInfo, cs::Signature poolSignature);
  void prepareMetaForSending(cs::RoundTable& roundTable);

  void sendRoundInfoRequest(uint8_t respondent);
  void getRoundInfoRequest(const uint8_t*, const size_t, const cs::RoundNumber, const cs::PublicKey&);
  void sendRoundInfoReply(const cs::PublicKey& target, bool has_requested_info);
  void getRoundInfoReply(const uint8_t* data, const size_t size, const cs::RoundNumber rNum,
                         const cs::PublicKey& respondent);
  bool tryResendRoundInfo(const cs::PublicKey& respondent, cs::RoundNumber rNum);

  // transaction's pack syncro
  void getPacketHashesRequest(const uint8_t*, const std::size_t, const cs::RoundNumber, const cs::PublicKey&);
  void getPacketHashesReply(const uint8_t*, const std::size_t, const cs::RoundNumber, const cs::PublicKey& sender);

  void getCharacteristic(const uint8_t* data, const size_t size, const cs::RoundNumber round, const cs::PublicKey& sender);

  void getWriterNotification(const uint8_t* data, const std::size_t size, const cs::PublicKey& sender);
  void applyNotifications();
  void writeBlock(csdb::Pool& newPool, size_t sequence, const cs::PublicKey& sender);

  bool isCorrectNotification(const uint8_t* data, const std::size_t size);
  void sendWriterNotification();

  cs::Bytes createNotification(const cs::PublicKey& writerPublicKey);
  void createBlockValidatingPacket(const cs::PoolMetaInfo& poolMetaInfo, const cs::Characteristic& characteristic,
                                   const cs::Signature& signature, const cs::Notifications& notifications);

  // syncro get functions
  void getBlockRequest(const uint8_t*, const size_t, const cs::PublicKey& sender);
  void getBlockReply(const uint8_t*, const size_t, const cs::PublicKey& sender);

  // outcoming requests forming
  void sendVector(const cs::HashVector&);
  void sendMatrix(const cs::HashMatrix&);
  void sendHash(const csdb::PoolHash&, const cs::PublicKey&);

  // transaction's pack syncro
  void sendTransactionsPacket(const cs::TransactionsPacket& packet);
  void sendPacketHashesRequest(const cs::Hashes& hashes, const cs::RoundNumber round);
  void sendPacketHashesRequestToRandomNeighbour(const cs::Hashes& hashes, const cs::RoundNumber round);
  void sendPacketHashesReply(const cs::Packets& packets, const cs::RoundNumber round, const cs::PublicKey& target);
  void resetNeighbours();

  // syncro send functions
  void sendBlockReply(const cs::PoolsBlock& poolsBlock, const cs::PublicKey& target, uint32_t packCounter);

  // start new round
  void sendRoundTable(const cs::RoundTable& round);

  template <typename... Args>
  bool sendNeighbours(const cs::PublicKey& target, const MsgTypes& msgType, const cs::RoundNumber round, Args&&... args);

  template <typename... Args>
  void sendNeighbours(const ConnectionPtr& target, const MsgTypes& msgType, const cs::RoundNumber round, Args&&... args);

  template <class... Args>
  void sendBroadcast(const MsgTypes& msgType, const cs::RoundNumber round, Args&&... args);

  template <class... Args>
  void tryToSendDirect(const cs::PublicKey& target, const MsgTypes& msgType, const cs::RoundNumber round, Args&&... args);

  template <class... Args>
  bool sendToRandomNeighbour(const MsgTypes& msgType, const cs::RoundNumber round, Args&&... args);

  void flushCurrentTasks();
  void becomeWriter();
  void initNextRound(std::vector<cs::PublicKey>&& confidantNodes);
  void initNextRound(const cs::RoundTable& roundTable);
  void onRoundStart(const cs::RoundTable& roundTable);
  bool isPoolsSyncroStarted();

  enum MessageActions {
    Process,
    Postpone,
    Drop
  };

  MessageActions chooseMessageAction(const cs::RoundNumber, const MsgTypes);

  const cs::PublicKey& getNodeIdKey() const {
    return nodeIdKey_;
  }

  NodeLevel getNodeLevel() const {
    return myLevel_;
  }

  uint32_t getRoundNumber();
  uint8_t getConfidantNumber();

  BlockChain& getBlockChain() {
    return bc_;
  }

  const BlockChain& getBlockChain() const {
    return bc_;
  }

  slv2::SolverCore* getSolver() {
    return solver_;
  }

  const slv2::SolverCore* getSolver() const {
    return solver_;
  }

#ifdef NODE_API
  csconnector::connector& getConnector() {
    return api_;
  }
#endif

public slots:
  void processTimer();
  void onTransactionsPacketFlushed(const cs::TransactionsPacket& packet);
  void sendBlockRequest(const ConnectionPtr& target, const cs::PoolsRequestedSequences sequences, uint32_t packCounter);

private:
  bool init();
  void createRoundPackage(const cs::RoundTable& roundTable,
    const cs::PoolMetaInfo& poolMetaInfo,
    const cs::Characteristic& characteristic,
    const cs::Signature& signature/*,
    const cs::Notifications& notifications*/);
  void storeRoundPackageData(const cs::RoundTable& roundTable,
      const cs::PoolMetaInfo& poolMetaInfo,
      const cs::Characteristic& characteristic,
      const cs::Signature& signature/*,
      const cs::Notifications& notifications*/);

  // signature verification
  bool checkKeysFile();
  std::pair<cs::PublicKey, cs::PrivateKey> generateKeys();
  bool checkKeysForSignature(const cs::PublicKey&, const cs::PrivateKey&);
  void logPool(csdb::Pool& pool);

  // pool sync helpers
  void blockchainSync();

  //void addPoolMetaToMap(cs::PoolSyncMeta&& meta, csdb::Pool::sequence_t sequence);
  // obsolete:
  void processMetaMap()
  {
    getBlockChain().testCachedBlocks();
  }

  bool readRoundData(cs::RoundTable& roundTable);

  void onRoundStartConveyer(cs::RoundTable&& roundTable);

  // conveyer
  void processPacketsRequest(cs::Hashes&& hashes, const cs::RoundNumber round, const cs::PublicKey& sender);
  void processPacketsReply(cs::Packets&& packets, const cs::RoundNumber round);
  void processTransactionsPacket(cs::TransactionsPacket&& packet);

  // pool sync progress
  static void showSyncronizationProgress(csdb::Pool::sequence_t lastWrittenSequence,
                                         csdb::Pool::sequence_t globalSequence);

  template <typename T, typename... Args>
  void writeDefaultStream(const T& value, Args&&... args);

  template <typename T>
  void writeDefaultStream(const T& value);

  template <typename... Args>
  void sendBroadcast(const cs::PublicKey& target, const MsgTypes& msgType, const cs::RoundNumber round, Args&&... args);

  template <typename... Args>
  void sendBroadcastImpl(const MsgTypes& msgType, const cs::RoundNumber round, Args&&... args);

  // TODO: C++ 17 static inline?
  static const csdb::Address genesisAddress_;
  static const csdb::Address startAddress_;

  const cs::PublicKey nodeIdKey_;
  bool good_ = true;

  // file names for crypto public/private keys
  inline const static std::string privateKeyFileName_ = "NodePrivate.txt";
  inline const static std::string publicKeyFileName_ = "NodePublic.txt";

  // current round state
  cs::RoundNumber roundNum_ = 0;
  NodeLevel myLevel_;

  cs::Byte myConfidantIndex_;

  // main cs storage
  BlockChain bc_;

  // appidional dependencies
  slv2::SolverCore* solver_;
  Transport* transport_;
  cs::PoolSynchronizer* poolSynchronizer_;

#ifdef MONITOR_NODE
  csstats::csstats stats_;
#endif

#ifdef NODE_API
  csconnector::connector api_;
#endif

  RegionAllocator allocator_;
  RegionAllocator packStreamAllocator_;

  size_t lastStartSequence_;
  bool blocksReceivingStarted_ = false;

  // serialization/deserialization entities
  cs::IPackStream istream_;
  cs::OPackStream ostream_;

  // sends transactions blocks to network
  cs::Timer sendingTimer_;

  // sync meta
  cs::PoolMetaMap poolMetaMap_;  // active pool meta information

  // round package sent data storage
  struct SentRoundData {
    cs::RoundTable roundTable;
    cs::PoolMetaInfo poolMetaInfo;
    cs::Characteristic characteristic;
    cs::Signature poolSignature;
    cs::Notifications notifications;
  };

  SentRoundData lastSentRoundData_;
};

std::ostream& operator<<(std::ostream& os, NodeLevel nodeLevel);

#endif  // NODE_HPP
