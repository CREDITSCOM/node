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
#include "roundstat.hpp"

class Transport;

namespace cs {
class SolverCore;
}

namespace cs {
class PoolSynchronizer;
class Spammer;
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
  void runSpammer();

  // incoming requests processing
  void getBigBang(const uint8_t* data, const size_t size, const cs::RoundNumber rNum, uint8_t type);
  void getRoundTableSS(const uint8_t* data, const size_t size, const cs::RoundNumber, uint8_t type = 0);
  void getTransactionsPacket(const uint8_t* data, const std::size_t size);
  void getNodeStopRequest(const uint8_t* data, const std::size_t size);

  // SOLVER3 methods
  void getStageOne(const uint8_t* data, const size_t size, const cs::PublicKey& sender);
  void getStageTwo(const uint8_t* data, const size_t size, const cs::PublicKey& sender);
  void getStageThree(const uint8_t* data, const size_t size, const cs::PublicKey& sender);
  void getRoundTable(const uint8_t* data, const size_t size, const cs::RoundNumber, const cs::PublicKey& sender);

  void sendStageReply(const uint8_t sender, const cscrypto::Signature& signature, const MsgTypes msgType, const uint8_t requester);
  void sendStageOne(cs::StageOne&);

  // sends StageOne request to respondent about required
  void getHash(const uint8_t* data, const size_t size, cs::RoundNumber rNum, const cs::PublicKey& sender);
  void getStageOneRequest(const uint8_t* data, const size_t size, const cs::PublicKey& requester);
   
  void sendStageTwo(cs::StageTwo&);
  void getStageTwoRequest(const uint8_t* data, const size_t size, const cs::PublicKey& requester);

  void sendStageThree(cs::StageThree&);
  void getStageThreeRequest(const uint8_t* data, const size_t size, const cs::PublicKey& requester);

  void getStageRequest(const MsgTypes msgType, const uint8_t* data, const size_t size, const cs::PublicKey& requester);
  void stageRequest(MsgTypes msgType, uint8_t respondent, uint8_t required);
  void sendHash(cs::RoundNumber round);

  const cs::ConfidantsKeys& confidants() const;

  void onRoundStart(const cs::RoundTable& roundTable);
  void startConsensus();

  void sendRoundTable(cs::RoundTable& roundTable, cs::PoolMetaInfo poolMetaInfo, const cs::Signature& poolSignature);
  void prepareMetaForSending(cs::RoundTable& roundTable, std::string timeStamp);

  //smart-contracts consensus stages sending and getting 

  // handle mismatch between own round & global round, calling code should detect mismatch before calling to the method
  void handleRoundMismatch(const cs::RoundTable& global_table);

  // broadcast request for next round, to call after long timeout
  void sendNextRoundRequest();

  // send request for next round info from trusted node specified by index in list
  void sendRoundTableRequest(uint8_t respondent);

  // send request for next round info from node specified node
  void sendRoundTableRequest(const cs::PublicKey& respondent);
  void getRoundTableRequest(const uint8_t*, const size_t, const cs::RoundNumber, const cs::PublicKey&);
  void sendRoundTableReply(const cs::PublicKey& target, bool has_requested_info);
  void getRoundTableReply(const uint8_t* data, const size_t size, const cs::PublicKey& respondent);
  bool tryResendRoundTable(std::optional<const cs::PublicKey> respondent, cs::RoundNumber rNum);

  // transaction's pack syncro
  void getPacketHashesRequest(const uint8_t*, const std::size_t, const cs::RoundNumber, const cs::PublicKey&);
  void getPacketHashesReply(const uint8_t*, const std::size_t, const cs::RoundNumber, const cs::PublicKey& sender);

  void getCharacteristic(const uint8_t* data, const size_t size, const cs::RoundNumber round, const cs::PublicKey& sender);

  // syncro get functions
  void getBlockRequest(const uint8_t*, const size_t, const cs::PublicKey& sender);
  void getBlockReply(const uint8_t*, const size_t, const cs::PublicKey& sender);

  // transaction's pack syncro
  void sendTransactionsPacket(const cs::TransactionsPacket& packet);
  void sendPacketHashesRequest(const cs::PacketsHashes& hashes, const cs::RoundNumber round, uint32_t requestStep);
  void sendPacketHashesRequestToRandomNeighbour(const cs::PacketsHashes& hashes, const cs::RoundNumber round);
  void sendPacketHashesReply(const cs::Packets& packets, const cs::RoundNumber round, const cs::PublicKey& target);
  void resetNeighbours();

  // syncro send functions
  void sendBlockReply(const cs::PoolsBlock& poolsBlock, const cs::PublicKey& target, uint32_t packCounter);

  void flushCurrentTasks();
  void becomeWriter();

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
    return blockChain_;
  }

  const BlockChain& getBlockChain() const {
    return blockChain_;
  }

  cs::SolverCore* getSolver() {
    return solver_;
  }

  const cs::SolverCore* getSolver() const {
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
  void sendBlockRequest(const ConnectionPtr target, const cs::PoolsRequestedSequences sequences, uint32_t packCounter);

private:
  bool init();
  void createRoundPackage(const cs::RoundTable& roundTable, const cs::PoolMetaInfo& poolMetaInfo,
                          const cs::Characteristic& characteristic, const cs::Signature& signature);

  void storeRoundPackageData(const cs::RoundTable& roundTable, const cs::PoolMetaInfo& poolMetaInfo,
                             const cs::Characteristic& characteristic, const cs::Signature& signature);

  // signature verification
  bool checkKeysFile();
  std::pair<cs::PublicKey, cs::PrivateKey> generateKeys();
  bool checkKeysForSignature(const cs::PublicKey&, const cs::PrivateKey&);

  // pool sync helpers
  void blockchainSync();

  bool readRoundData(cs::RoundTable& roundTable);
  void reviewConveyerHashes();

  // conveyer
  void processPacketsRequest(cs::PacketsHashes&& hashes, const cs::RoundNumber round, const cs::PublicKey& sender);
  void processPacketsReply(cs::Packets&& packets, const cs::RoundNumber round);
  void processTransactionsPacket(cs::TransactionsPacket&& packet);

  /// sending interace methods

  // default methods without flags
  template<typename... Args>
  void sendDefault(const cs::PublicKey& target, const MsgTypes msgType, const cs::RoundNumber round, Args&&... args);

  // to neighbour
  template <typename... Args>
  bool sendToNeighbour(const cs::PublicKey& target, const MsgTypes msgType, const cs::RoundNumber round, Args&&... args);

  template <typename... Args>
  void sendToNeighbour(const ConnectionPtr target, const MsgTypes msgType, const cs::RoundNumber round, Args&&... args);

  template <class... Args>
  void tryToSendDirect(const cs::PublicKey& target, const MsgTypes msgType, const cs::RoundNumber round, Args&&... args);

  template <class... Args>
  bool sendToRandomNeighbour(const MsgTypes msgType, const cs::RoundNumber round, Args&&... args);

  template <class... Args>
  void sendToConfidants(const MsgTypes msgType, const cs::RoundNumber round, Args&&... args);

  // to neighbours
  template<typename... Args>
  bool sendToNeighbours(const MsgTypes msgType, const cs::RoundNumber round, Args&&... args);

  // broadcast
  template <class... Args>
  void sendBroadcast(const MsgTypes msgType, const cs::RoundNumber round, Args&&... args);

  template <typename... Args>
  void sendBroadcast(const cs::PublicKey& target, const MsgTypes& msgType, const cs::RoundNumber round, Args&&... args);

  template <typename... Args>
  void sendBroadcastImpl(const MsgTypes& msgType, const cs::RoundNumber round, Args&&... args);

  // write values to stream
  template <typename T, typename... Args>
  void writeDefaultStream(const T& value, Args&&... args);

  template <typename T>
  void writeDefaultStream(const T& value);

  // TODO: C++ 17 static inline?
  static const csdb::Address genesisAddress_;
  static const csdb::Address startAddress_;

  const cs::PublicKey nodeIdKey_;
  bool good_ = true;

  // file names for crypto public/private keys
  inline const static std::string privateKeyFileName_ = "NodePrivate.txt";
  inline const static std::string publicKeyFileName_ = "NodePublic.txt";

  // current round state
  cs::RoundNumber roundNumber_ = 0;
  NodeLevel myLevel_;

  cs::Byte myConfidantIndex_;

  // main cs storage
  BlockChain blockChain_;

  // appidional dependencies
  cs::SolverCore* solver_;
  Transport* transport_;
  std::unique_ptr<cs::Spammer> spammer_;

#ifdef MONITOR_NODE
  csstats::csstats stats_;
#endif

#ifdef NODE_API
  csconnector::connector api_;
#endif

  RegionAllocator allocator_;
  RegionAllocator packStreamAllocator_;

  size_t lastStartSequence_;
  uint32_t startPacketRequestPoint_ = 0;

  // ms timeout
  inline static const uint32_t packetRequestStep_ = 450;
  inline static const size_t maxPacketRequestSize_ = 1000;

  bool blocksReceivingStarted_ = false;

  // serialization/deserialization entities
  cs::IPackStream istream_;
  cs::OPackStream ostream_;

  cs::PoolSynchronizer* poolSynchronizer_;

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

  std::vector<cs::Bytes> stageOneMessage_;
  std::vector<cs::Bytes> stageTwoMessage_;
  std::vector<cs::Bytes> stageThreeMessage_;

  std::vector<cs::Bytes> stageOneSmartsMessage_;
  std::vector<cs::Bytes> stageTwoSmartsMessage_;
  std::vector<cs::Bytes> stageThreeSmartsMessage_;

  SentRoundData lastSentRoundData_;

  // round stat
  cs::RoundStat stat_;
};

std::ostream& operator<<(std::ostream& os, NodeLevel nodeLevel);

#endif  // NODE_HPP
