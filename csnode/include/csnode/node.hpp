/* Send blaming letters to @yrtimd */
#ifndef __NODE_HPP__
#define __NODE_HPP__

#include <iostream>
#include <memory>
#include <string>

#include <csstats.h>
#include <csconnector/csconnector.h>
#include <client/config.hpp>

#include <csnode/datastream.h>
#include <csnode/dynamicbuffer.h>
#include <lib/system/keys.hpp>
#include <lib/system/timer.hpp>

#include <net/neighbourhood.hpp>

#include "blockchain.hpp"
#include "packstream.hpp"

class Transport;

namespace slv2 {
  class SolverCore;
}

class Node {
public:
  explicit Node(const Config&);
  ~Node();

  bool isGood() const {
    return good_;
  }

  void run();

  // incoming requests processing
  void getRoundTableSS(const uint8_t*, const size_t, const cs::RoundNumber, uint8_t type = 0);
  void getVector(const uint8_t*, const size_t, const cs::PublicKey& sender);
  void getMatrix(const uint8_t*, const size_t, const cs::PublicKey& sender);
  void getHash(const uint8_t*, const size_t, const cs::PublicKey& sender);
  void getTransactionsPacket(const uint8_t*, const std::size_t);

  //SOLVER3 methods
  void getStageOne(const uint8_t*, const size_t, const cs::PublicKey& sender);
  void getStageTwo(const uint8_t*, const size_t, const cs::PublicKey& sender);
  void getStageThree(const uint8_t*, const size_t, const cs::PublicKey& sender);
  void getRoundInfo(const uint8_t*, const size_t, const cs::RoundNumber, const cs::PublicKey& sender);
  void getRoundInfo_(const uint8_t *, const size_t , const cs::RoundNumber, const cs::PublicKey&);

  //SOLVER3 methods
  void sendStageOne(cs::StageOne&);
  // sends StageOne request to respondent about required
  void getHash_V3(const uint8_t* data, const size_t size, const cs::PublicKey& sender);
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

  void sendHash_V3();

  const cs::ConfidantsKeys confidants() const
  {
      return cs::Conveyer::instance().roundTable().confidants;
  }

  void onRoundStart_V3(const cs::RoundTable& roundTable);

  void passBlockToSolver(csdb::Pool& pool, const cs::PublicKey& sender);
  void addCompressedPoolToPack(const csdb::Pool& pool);
  csdb::Pool getCompressedPoolFromPack();
  void composeMessageWithBlock1(const csdb::Pool& pool);

  void sendRoundInfo(const std::vector<cs::PublicKey>& confidantNodes,
      const csdb::Pool& poolToVerify,
      const csdb::Pool& newPool,
      const std::vector <cs::StageThree>& stageThreeStorage);

  void sendRoundInfo_(const cs::RoundTable& roundTable);

  void sendRoundInfoRequest(uint8_t respondent);
  void getRoundInfoReply(const uint8_t* data, const size_t size, const cs::RoundNumber rNum, const cs::PublicKey& respondent);
  void sendRoundInfoReply(uint8_t, uint8_t);
  void getRoundInfoRequest(const uint8_t*, const size_t, const cs::RoundNumber, const cs::PublicKey&);

  // transaction's pack syncro
  void getPacketHashesRequest(const uint8_t*, const std::size_t, const cs::RoundNumber, const cs::PublicKey&);
  void getPacketHashesReply(const uint8_t*, const std::size_t, const cs::RoundNumber, const cs::PublicKey& sender);

  void getRoundTable(const uint8_t*, const size_t, const cs::RoundNumber);
  void getCharacteristic(const uint8_t* data, const size_t size, const cs::RoundNumber round, const cs::PublicKey& sender);

  void onTransactionsPacketFlushed(const cs::TransactionsPacket& packet);

  void getWriterNotification(const uint8_t* data, const std::size_t size, const cs::PublicKey& sender);
  void applyNotifications();
  void writeBlock(csdb::Pool& newPool, size_t sequence, const cs::PublicKey& sender);

  bool isCorrectNotification(const uint8_t* data, const std::size_t size);
  void sendWriterNotification();

  cs::Bytes createNotification();
  cs::Bytes createBlockValidatingPacket(const cs::PoolMetaInfo& poolMetaInfo, const cs::Characteristic& characteristic,
                                        const cs::Signature& signature, const cs::Notifications& notifications);

  // syncro get functions
  void getBlockRequest(const uint8_t*, const size_t, const cs::PublicKey& sender);
  void getBlockReply(const uint8_t*, const size_t);

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
  void sendBlockRequest();
  void sendBlockRequest(const std::vector<csdb::Pool::sequence_t>& sequences);
  void sendBlockReply(const csdb::Pool&, const cs::PublicKey& target);

  // start new round
  void sendRoundTable(const cs::RoundTable& round);

  template<class... Args>
  bool sendNeighbours(const cs::PublicKey& target, const MsgTypes& msgType, const cs::RoundNumber round, const Args&... args);
  bool sendNeighbours(const cs::PublicKey& target, const MsgTypes& msgType, const cs::RoundNumber round, const cs::Bytes& bytes);
  void sendNeighbours(const ConnectionPtr& target, const MsgTypes& msgType, const cs::RoundNumber round, const cs::Bytes& bytes);

  template <class... Args>
  void sendBroadcast(const MsgTypes& msgType, const cs::RoundNumber round, const Args&... args);
  void sendBroadcast(const MsgTypes& msgType, const cs::RoundNumber round, const cs::Bytes& bytes);

  template <class... Args>
  void tryToSendDirect(const cs::PublicKey& target, const MsgTypes& msgType, const cs::RoundNumber round, const Args&... args);
  void tryToSendDirect(const cs::PublicKey& target, const MsgTypes& msgType, const cs::RoundNumber round, const cs::Bytes& bytes);

  template <class... Args>
  bool sendToRandomNeighbour(const MsgTypes& msgType, const cs::RoundNumber round, const Args&... args);
  bool sendToRandomNeighbour(const MsgTypes& msgType, const cs::RoundNumber round, const cs::Bytes& bytes);

  void flushCurrentTasks();
  void becomeWriter();
  void initNextRound( std::vector<cs::PublicKey>&& confidantNodes);
  void initNextRound(const cs::RoundTable& roundTable);

  bool getSyncroStarted();

  enum MessageActions {
    Process,
    Postpone,
    Drop
  };

  MessageActions chooseMessageAction(const cs::RoundNumber, const MsgTypes);

  const cs::PublicKey& getPublicKey() const {
    return myPublicKey_;
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
  void onRoundStart(const cs::RoundTable& roundTable);
private:
  bool init();

  // signature verification
  bool checkKeysFile();
  std::pair<cs::PublicKey, cs::PrivateKey> generateKeys();
  bool checkKeysForSignature(const cs::PublicKey&, const cs::PrivateKey&);

  // pool sync helpers
  void blockchainSync();
  void processPoolSync();

  void addPoolMetaToMap(cs::PoolSyncMeta&& meta, csdb::Pool::sequence_t sequence);
  void processMetaMap();

  bool readRoundData(cs::RoundTable& roundTable);

  void onRoundStartConveyer(cs::RoundTable&& roundTable);

  // conveyer
  void processPacketsRequest(cs::Hashes&& hashes, const cs::RoundNumber round, const cs::PublicKey& sender);
  void processPacketsReply(cs::Packets&& packets, const cs::RoundNumber round);
  void processTransactionsPacket(cs::TransactionsPacket&& packet);

  // pool sync progress
  static void showSyncronizationProgress(csdb::Pool::sequence_t lastWrittenSequence, csdb::Pool::sequence_t globalSequence);

  template <class T, class... Args>
  void writeDefaultStream(cs::DataStream& stream, const T& value, const Args&... args);

  template<class T>
  void writeDefaultStream(cs::DataStream& stream, const T& value);

  void sendBroadcast(const cs::PublicKey& target, const MsgTypes& msgType, const cs::RoundNumber round, const cs::Bytes& bytes);
  void sendBroadcastImpl(const MsgTypes& msgType, const cs::RoundNumber round, const cs::Bytes& bytes);

  // TODO: C++ 17 static inline?
  static const csdb::Address genesisAddress_;
  static const csdb::Address startAddress_;
  static const csdb::Address spammerAddress_;

  const cs::PublicKey myPublicKey_;
  bool good_ = true;

  // syncro variables
  bool isSyncroStarted_ = false;
  bool isAwaitingSyncroBlock_ = false;
  uint32_t awaitingRecBlockCount_ = 0;

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

  IPackStream istream_;
  OPackStream ostream_;

  // sends transactions blocks to network
  cs::Timer sendingTimer_;

  // sync meta
  csdb::Pool::sequence_t sendBlockRequestSequence_;
  cs::PoolMetaMap poolMetaMap_;   // active pool meta information
  std::map<csdb::Pool::sequence_t, csdb::Pool> poolSyncRequestMap_;   // meta information of pool sync from nodes
  cs::RoundNumber roundToSync_ = 0;

  inline static const std::size_t maxPoolCountToSync_ = 10;
};

std::ostream& operator<< (std::ostream& os, NodeLevel nodeLevel);

#endif  // __NODE_HPP__
