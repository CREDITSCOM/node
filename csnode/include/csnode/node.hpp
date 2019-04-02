#ifndef NODE_HPP
#define NODE_HPP

#include <iostream>
#include <memory>
#include <string>

#include <csconnector/csconnector.hpp>
#include <csstats.hpp>
#include <client/config.hpp>

#include <csnode/conveyer.hpp>
#include <lib/system/timer.hpp>

#include <net/neighbourhood.hpp>

#include "blockchain.hpp"
#include "packstream.hpp"
#include "roundstat.hpp"
#include "confirmationlist.hpp"

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

  std::string getSenderText(const cs::PublicKey& sender);

  // incoming requests processing
  void getBigBang(const uint8_t* data, const size_t size, const cs::RoundNumber rNum);
  void getRoundTableSS(const uint8_t* data, const size_t size, const cs::RoundNumber);
  void getKeySS(const cs::PublicKey& key);
  void getTransactionsPacket(const uint8_t* data, const std::size_t size);
  void getNodeStopRequest(const uint8_t* data, const std::size_t size);
  bool canBeTrusted();

  // SOLVER3 methods
  void getRoundTable(const uint8_t* data, const size_t size, const cs::RoundNumber, const cs::PublicKey& sender);
  void sendHash(cs::RoundNumber round);
  void getHash(const uint8_t* data, const size_t size, cs::RoundNumber rNum, const cs::PublicKey& sender);
  void sendHashReply(const csdb::PoolHash& hash, const cs::PublicKey& respondent);
  void getHashReply(const uint8_t* data, const size_t size, cs::RoundNumber rNum, const cs::PublicKey& sender);

  //consensus communication
  void sendStageOne(cs::StageOne&);
  void sendStageTwo(cs::StageTwo&);
  void sendStageThree(cs::StageThree&);

  void getStageOne(const uint8_t* data, const size_t size, const cs::PublicKey& sender);
  void getStageTwo(const uint8_t* data, const size_t size, const cs::PublicKey& sender);
  void getStageThree(const uint8_t* data, const size_t size);

  void adjustStageThreeStorage();
  void stageRequest(MsgTypes msgType, uint8_t respondent, uint8_t required/*, uint8_t iteration*/);
  void getStageRequest(const MsgTypes msgType, const uint8_t* data, const size_t size, const cs::PublicKey& requester);
  void sendStageReply(const uint8_t sender, const cs::Signature& signature, const MsgTypes msgType, const uint8_t requester, cs::Bytes& message);

  //smart-contracts consensus communicatioin
  void sendSmartStageOne(const cs::ConfidantsKeys& smartConfidants, cs::StageOneSmarts& stageOneInfo);
  void getSmartStageOne(const uint8_t* data, const size_t size, const cs::RoundNumber rNum,  const cs::PublicKey& sender);
  void sendSmartStageTwo(const cs::ConfidantsKeys& smartConfidants, cs::StageTwoSmarts& stageTwoInfo);
  void getSmartStageTwo(const uint8_t* data, const size_t size, const cs::RoundNumber rNum, const cs::PublicKey& sender);
  void sendSmartStageThree(const cs::ConfidantsKeys& smartConfidants, cs::StageThreeSmarts& stageThreeInfo);
  void getSmartStageThree(const uint8_t* data, const size_t size, const cs::RoundNumber rNum, const cs::PublicKey& sender);
  void smartStageEmptyReply(uint8_t requesterNumber);
  void smartStageRequest(MsgTypes msgType, cs::Sequence smartRound, uint32_t startTransaction, cs::PublicKey confidant, uint8_t respondent, uint8_t required);
  void getSmartStageRequest(const MsgTypes msgType, const uint8_t* data, const size_t size, const cs::PublicKey& requester);
  void sendSmartStageReply(const cs::Bytes& message, const cs::RoundNumber smartRNum, const cs::Signature& signature
      , const MsgTypes msgType, const cs::PublicKey& requester);

  void addSmartConsensus(cs::Sequence block, uint32_t transaction);
  void removeSmartConsensus(cs::Sequence block, uint32_t transaction);
  void checkForSavedSmartStages(cs::Sequence block, uint32_t transaction);

  void sendSmartReject(const std::vector< std::pair<cs::Sequence, uint32_t> >& ref_list);
  void getSmartReject(const uint8_t* data, const size_t size, const cs::RoundNumber rNum, const cs::PublicKey& sender);

  csdb::PoolHash spoileHash(const csdb::PoolHash& hashToSpoil);
  csdb::PoolHash spoileHash(const csdb::PoolHash& hashToSpoil, const cs::PublicKey& pKey);

  cs::ConfidantsKeys retriveSmartConfidants(const cs::Sequence startSmartRoundNumber) const;

  void onRoundStart(const cs::RoundTable& roundTable);
  void startConsensus();

  void prepareRoundTable(cs::RoundTable& roundTable, const cs::PoolMetaInfo& poolMetaInfo, cs::StageThree& st3);
  bool receivingSignatures(const cs::Bytes& sigBytes, const cs::Bytes& roundBytes, const cs::RoundNumber rNum
      , const cs::Bytes& trustedMask, const cs::ConfidantsKeys& newConfidants
      , cs::Signatures& poolSignatures);
  void addRoundSignature(const cs::StageThree& st3);
  //smart-contracts consensus stages sending and getting

  // handle mismatch between own round & global round, calling code should detect mismatch before calling to the method
  void handleRoundMismatch(const cs::RoundTable& global_table);

  // send request for next round info from trusted node specified by index in list
  void sendRoundTableRequest(uint8_t respondent);

  // send request for next round info from node specified node
  void sendRoundTableRequest(const cs::PublicKey& respondent);
  void getRoundTableRequest(const uint8_t*, const size_t, const cs::RoundNumber, const cs::PublicKey&);
  void sendRoundTableReply(const cs::PublicKey& target, bool hasRequestedInfo);
  void getRoundTableReply(const uint8_t* data, const size_t size, const cs::PublicKey& respondent);
  // called by solver, review required:
  bool tryResendRoundTable(const cs::PublicKey& target, const cs::RoundNumber rNum);
  void sendRoundTable();

  // transaction's pack syncro
  void getPacketHashesRequest(const uint8_t*, const std::size_t, const cs::RoundNumber, const cs::PublicKey&);
  void getPacketHashesReply(const uint8_t*, const std::size_t, const cs::RoundNumber, const cs::PublicKey& sender);

  void getCharacteristic(const uint8_t* data, const size_t size, const cs::RoundNumber round,
                         const cs::PublicKey& sender, cs::Signatures&& poolSignatures, cs::Bytes realTrusted);

  // syncro get functions
  void getBlockRequest(const uint8_t*, const size_t, const cs::PublicKey& sender);
  void getBlockReply(const uint8_t*, const size_t);

  // transaction's pack syncro
  void sendTransactionsPacket(const cs::TransactionsPacket& packet);
  void sendPacketHashesRequest(const cs::PacketsHashes& hashes, const cs::RoundNumber round, uint32_t requestStep);
  void sendPacketHashesRequestToRandomNeighbour(const cs::PacketsHashes& hashes, const cs::RoundNumber round);
  void sendPacketHashesReply(const cs::Packets& packets, const cs::RoundNumber round, const cs::PublicKey& target);

  //smarts consensus additional functions:

  // syncro send functions
  void sendBlockReply(const cs::PoolsBlock& poolsBlock, const cs::PublicKey& target, std::size_t packCounter);

  void flushCurrentTasks();
  void becomeWriter();

  bool isPoolsSyncroStarted();

  //void smartStagesStorageClear(size_t cSize);
  
  std::optional<cs::TrustedConfirmation> getConfirmation(cs::RoundNumber rNum) const
  {
    return confirmationList.find(rNum);
  }
  
  enum Level {
    Normal,
    Confidant,
    Main,
    Writer
  };

  enum MessageActions {
    Process,
    Postpone,
    Drop
  };

  MessageActions chooseMessageAction(const cs::RoundNumber, const MsgTypes);

  const cs::PublicKey& getNodeIdKey() const {
    return nodeIdKey_;
  }

  Level getNodeLevel() const {
    return myLevel_;
  }

  uint8_t getConfidantNumber() const{
    return myConfidantIndex_;
  }

  uint8_t subRound() const {
    return subRound_;
  }

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
  csconnector::connector *getConnector() {
    return api_.get();
  }
#endif

  template <typename T>
  using SmartsSignal = cs::Signal<void(T&, bool)>;
  using SmartStageRequestSignal = cs::Signal<void(uint8_t, cs::Sequence, uint32_t, uint8_t, uint8_t, cs::PublicKey&)>;

public signals:
  SmartsSignal<cs::StageOneSmarts> gotSmartStageOne;
  SmartsSignal<cs::StageTwoSmarts> gotSmartStageTwo;
  SmartsSignal<cs::StageThreeSmarts> gotSmartStageThree;
  SmartStageRequestSignal receivedSmartStageRequest;
  cs::Signal<void(const std::vector< std::pair<cs::Sequence, uint32_t> >&)> gotRejectedContracts;

public slots:
  void processTimer();
  void onTransactionsPacketFlushed(const cs::TransactionsPacket& packet);
  void sendBlockRequest(const ConnectionPtr target, const cs::PoolsRequestedSequences& sequences, std::size_t packCounter);

private:
  bool init(const Config& config);
  void sendRoundPackage(const cs::PublicKey& target);
  void sendRoundPackageToAll();

  void storeRoundPackageData(const cs::RoundTable& roundTable, const cs::PoolMetaInfo& poolMetaInfo,
                             const cs::Characteristic& characteristic, cs::StageThree& st3);

  bool readRoundData(cs::RoundTable& roundTable, bool bang);
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
  
  //smarts
  template <class... Args>
  void sendToList(const std::vector<cs::PublicKey>& listMembers, const cs::Byte listExeption, const MsgTypes msgType, const cs::RoundNumber round, Args&&... args);

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
  template <typename... Args>
  void writeDefaultStream(Args&&... args);

  RegionPtr compressPoolsBlock(const cs::PoolsBlock& poolsBlock, std::size_t& realBinSize);
  cs::PoolsBlock decompressPoolsBlock(const uint8_t* data, const size_t size);

  // TODO: C++ 17 static inline?
  static const csdb::Address genesisAddress_;
  static const csdb::Address startAddress_;

  cs::PublicKey ssKey_;

  const cs::PublicKey nodeIdKey_;
  const cs::PrivateKey nodeIdPrivate_;
  bool good_ = true;

  // file names for crypto public/private keys
  inline const static std::string privateKeyFileName_ = "NodePrivate.txt";
  inline const static std::string publicKeyFileName_ = "NodePublic.txt";

  Level myLevel_;
  cs::Byte myConfidantIndex_;

  // main cs storage
  BlockChain blockChain_;

  // appidional dependencies
  cs::SolverCore* solver_;
  Transport* transport_;
  std::unique_ptr<cs::Spammer> spammer_;

#ifdef NODE_API
  std::unique_ptr<csconnector::connector> api_;
#endif

  RegionAllocator allocator_;
  RegionAllocator packStreamAllocator_;

  uint32_t startPacketRequestPoint_ = 0;

  // ms timeout
  inline static const uint32_t packetRequestStep_ = 450;
  inline static const size_t maxPacketRequestSize_ = 1000;

  // serialization/deserialization entities
  cs::IPackStream istream_;
  cs::OPackStream ostream_;

  cs::PoolSynchronizer* poolSynchronizer_;

  // sends transactions blocks to network
  cs::Timer sendingTimer_;
  cs::Byte subRound_;

  // round package sent data storage
  struct SentRoundData {
    cs::RoundTable table;
    cs::Byte subRound;
  };

  struct SentSignatures {
    cs::Signatures poolSignatures;
    cs::Signatures roundSignatures;
    cs::Signatures trustedConfirmation;
  };

  cs::Bytes lastRoundTableMessage_;
  cs::Bytes lastSignaturesMessage_;

  std::vector<cs::Bytes> stageOneMessage_;
  std::vector<cs::Bytes> stageTwoMessage_;
  std::vector<cs::Bytes> stageThreeMessage_;
  bool stageThreeSent = false;

  std::vector<cs::Bytes> smartStageOneMessage_;
  std::vector<cs::Bytes> smartStageTwoMessage_;
  std::vector<cs::Bytes> smartStageThreeMessage_;

  std::vector<cs::StageOneSmarts> smartStageOneStorage_;
  std::vector<cs::StageTwoSmarts> smartStageTwoStorage_;
  std::vector<cs::StageThreeSmarts> smartStageThreeStorage_;
  int corruptionLevel_ = 0;

  std::vector<cs::Stage> smartStageTemporary_;
  std::vector<std::pair<cs::Sequence, uint32_t>> activeSmartConsensuses_;

  SentRoundData lastSentRoundData_;
  SentSignatures lastSentSignatures_;

  std::vector<bool> badHashReplyCounter_;

  // round stat
  cs::RoundStat stat_;

  // confirmation list
  cs::ConfirmationList confirmationList;
};

std::ostream& operator<<(std::ostream& os, Node::Level nodeLevel);

#endif  // NODE_HPP
