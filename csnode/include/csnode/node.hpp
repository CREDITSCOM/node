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
namespace slv2 { class SolverCore; }

class Node {
public:
  static const std::string start_address_;
public:
  Node(const Config&);
  ~Node();

  bool isGood() const {
    return good_;
  }

  void run();

  /* Incoming requests processing */
  void getRoundTableSS(const uint8_t*, const size_t, const RoundNum, uint8_t type = 0);
  void getBigBang(const uint8_t*, const size_t, const RoundNum, uint8_t type);
  void getTransaction(const uint8_t*, const size_t);
  void getFirstTransaction(const uint8_t*, const size_t);
  void getTransactionsList(const uint8_t*, const size_t);
  void getVector(const uint8_t*, const size_t, const cs::PublicKey& sender);
  void getMatrix(const uint8_t*, const size_t, const cs::PublicKey& sender);
  void getBlock(const uint8_t*, const size_t, const cs::PublicKey& sender);
  void getHash(const uint8_t*, const size_t, const cs::PublicKey& sender);
  void getTransactionsPacket(const uint8_t*, const std::size_t);

  // transaction's pack syncro
  void getPacketHashesRequest(const uint8_t*, const std::size_t, const RoundNum, const cs::PublicKey&);
  void getPacketHashesReply(const uint8_t*, const std::size_t, const RoundNum, const cs::PublicKey& sender);

  void getRoundTable(const uint8_t*, const size_t, const RoundNum);
  void getCharacteristic(const uint8_t* data, const size_t size, const cs::PublicKey& sender);

  void onTransactionsPacketFlushed(const cs::TransactionsPacket& packet);

  void getWriterNotification(const uint8_t* data, const std::size_t size, const cs::PublicKey& senderPublicKey);
  void applyNotifications();
  void writeBlock(csdb::Pool newPool, size_t sequence, const cs::PublicKey &sender);

  bool isCorrectNotification(const uint8_t* data, const std::size_t size);
  void sendWriterNotification();

  cs::Bytes createNotification();
  cs::Bytes createBlockValidatingPacket(const cs::PoolMetaInfo& poolMetaInfo, const cs::Characteristic& characteristic,
                                        const cs::Signature& signature, const cs::Notifications& notifications);

  /*syncro get functions*/
  void getBlockRequest(const uint8_t*, const size_t, const cs::PublicKey& sender);
  void getBlockReply(const uint8_t*, const size_t);
  void getRoundTableRequest(const uint8_t* data, const size_t size, const cs::PublicKey& sender);

  void getBadBlock(const uint8_t*, const size_t, const cs::PublicKey& sender);

  /* Outcoming requests forming */
  void sendFirstTransaction(const csdb::Transaction&);
  void sendTransactionList(const csdb::Pool&);
  void sendVector(const cs::HashVector&);
  void sendMatrix(const cs::HashMatrix&);
  void sendBlock(const csdb::Pool&);
  void sendHash(const std::string&, const cs::PublicKey&);

  // transaction's pack syncro
  void sendTransactionsPacket(const cs::TransactionsPacket& packet);
  void sendPacketHashesRequest(const std::vector<cs::TransactionsPacketHash>& hashes, const cs::RoundNumber round);
  void sendPacketHashesReply(const cs::TransactionsPacket& packet, const cs::RoundNumber round, const cs::PublicKey& sender);
  void resetNeighbours();

  void sendBadBlock(const csdb::Pool& pool);

  /*syncro send functions*/
  void sendBlockRequest(uint32_t seq);
  void sendBlockReply(const csdb::Pool&, const cs::PublicKey&);
  void sendWritingConfirmation(const cs::PublicKey& node);
  void sendRoundTableRequest(size_t rNum);
  void sendRoundTable(const cs::RoundTable& round);

  template<class... Args>
  bool sendDirect(const cs::PublicKey& sender, const MsgTypes& msgType, const cs::RoundNumber round, const Args&... args);
  bool sendDirect(const cs::PublicKey& sender, const MsgTypes& msgType, const cs::RoundNumber round, const cs::Bytes& bytes);
  void sendDirect(const ConnectionPtr& connection, const MsgTypes& msgType, const cs::RoundNumber round, const cs::Bytes& bytes);

  template <class... Args>
  void sendBroadcast(const MsgTypes& msgType, const cs::RoundNumber round, const Args&... args);
  void sendBroadcast(const MsgTypes& msgType, const cs::RoundNumber round, const cs::Bytes& bytes);

  template <class... Args>
  bool sendToRandomNeighbour(const MsgTypes& msgType, const cs::RoundNumber round, const Args&... args);
  bool sendToRandomNeighbour(const MsgTypes& msgType, const cs::RoundNumber round, const cs::Bytes& bytes);

  void sendVectorRequest(const cs::PublicKey&);
  void sendMatrixRequest(const cs::PublicKey&);

  void sendTLRequest();
  void getTlRequest(const uint8_t* data, const size_t size);

  void getVectorRequest(const uint8_t* data, const size_t size);
  void getMatrixRequest(const uint8_t* data, const size_t size);

  void flushCurrentTasks();
  void becomeWriter();
  void initNextRound(const cs::RoundTable& roundTable);
  bool getSyncroStarted();

  enum MessageActions {
    Process,
    Postpone,
    Drop
  };

  MessageActions chooseMessageAction(const RoundNum, const MsgTypes);

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

private:
  bool init();

  // signature verification
  bool checkKeysFile();
  void generateKeys();
  bool checkKeysForSig();

  bool readRoundData(cs::RoundTable& roundTable);
  void onRoundStart(const cs::RoundTable& roundTable);
  void onRoundStartConveyer(cs::RoundTable&& roundTable);

  // conveyer
  void processPacketsRequest(cs::Hashes&& hashes, const cs::RoundNumber round, const cs::PublicKey& sender);
  void processPacketsReply(cs::TransactionsPacket&& packet, const cs::RoundNumber round);
  void processTransactionsPacket(cs::TransactionsPacket&& packet);

  void composeMessageWithBlock(const csdb::Pool&, const MsgTypes);
  void composeCompressed(const void*, const uint32_t, const MsgTypes);

  template <class T, class... Args>
  void writeDefaultStream(cs::DataStream& stream, const T& value, const Args&... args);

  template<class T>
  void writeDefaultStream(cs::DataStream& stream, const T& value);

  // Info
  static const csdb::Address genesisAddress_;
  static const csdb::Address startAddress_;
  static const csdb::Address spammerAddress_;
  const cs::PublicKey myPublicKey_;
  bool            good_ = true;

  // syncro variables
  bool syncro_started = false;
  uint32_t sendBlockRequestSequence;
  bool awaitingSyncroBlock = false;
  uint32_t awaitingRecBlockCount = 0;

  // signature variables
  std::vector<uint8_t> myPublicForSig;
  std::vector<uint8_t> myPrivateForSig;

  std::string rcvd_trx_fname = "rcvd.txt";
  std::string sent_trx_fname = "sent.txt";

  // Current round state
  RoundNum roundNum_ = 0;
  NodeLevel myLevel_;

  uint8_t myConfidantIndex_;

  // Resources
  BlockChain bc_;

  slv2::SolverCore* solver_;
  Transport* transport_;

#ifdef MONITOR_NODE
  csstats::csstats       stats_;
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

  /// sends transactions blocks to network
  cs::Timer sendingTimer_;

  static const uint8_t broadcastFlag_ = BaseFlags::Broadcast | BaseFlags::Fragmented | BaseFlags::Compressed;
  static const uint8_t directFlag_    = BaseFlags::Neighbors | BaseFlags::Broadcast | BaseFlags::Fragmented | BaseFlags::Compressed;
};

std::ostream& operator<< (std::ostream& os, NodeLevel nodeLevel);

#endif  // __NODE_HPP__
