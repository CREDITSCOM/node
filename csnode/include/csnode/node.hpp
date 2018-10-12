/* Send blaming letters to @yrtimd */
#ifndef __NODE_HPP__
#define __NODE_HPP__
#include <memory>

#include <csconnector/csconnector.h>
#include <csstats.h>
#include <client/config.hpp>

#include <csnode/datastream.h>
#include <csnode/dynamicbuffer.h>

#include "blockchain.hpp"
#include "packstream.hpp"

enum NodeLevel { Normal, Confidant, Main, Writer };

class Transport;

namespace cs {
class Solver;
}

class Node {
 public:
  Node(const Config&);
  ~Node();

  bool isGood() const {
    return good_;
  }

  void run(const Config&);

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
  void getPacketHashesRequest(const uint8_t*, const std::size_t, const cs::PublicKey& sender);
  void getPacketHashesReply(const uint8_t*, const std::size_t);

  void getRoundTable(const uint8_t*, const size_t, const RoundNum);
  void getCharacteristic(const uint8_t* data, const size_t size, const cs::PublicKey& sender);

  void getNotification(const uint8_t* data, const std::size_t size, const cs::PublicKey& senderPublicKey);
  void applyNotifications();

  bool isCorrectNotification(const uint8_t* data, const std::size_t size);
  void sendWriterNotification();

  cs::Bytes createNotification();
  cs::Bytes createBlockValidatingPacket(const cs::PoolMetaInfo& poolMetaInfo, const cs::Characteristic& characteristic,
                                        const cs::Signature& signature, const cs::Notifications& notifications);

  /*syncro get functions*/
  void getBlockRequest(const uint8_t*, const size_t, const cs::PublicKey& sender);
  void getBlockReply(const uint8_t*, const size_t);
  void getWritingConfirmation(const uint8_t* data, const size_t size, const cs::PublicKey& sender);
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
  void sendPacketHashesRequest(const std::vector<cs::TransactionsPacketHash>& hashes);
  void sendPacketHashesReply(const cs::TransactionsPacket& packet, const cs::PublicKey& sender);

  void sendBadBlock(const csdb::Pool& pool);
  void sendCharacteristic(const cs::PoolMetaInfo& emptyMetaPool, const cs::Characteristic& characteristic);

  /*syncro send functions*/
  void sendBlockRequest(uint32_t seq);
  void sendBlockReply(const csdb::Pool&, const cs::PublicKey&);
  void sendWritingConfirmation(const cs::PublicKey& node);
  void sendRoundTableRequest(size_t rNum);
  void sendRoundTable(const cs::RoundTable& round);

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

  enum MessageActions { Process, Postpone, Drop };
  MessageActions chooseMessageAction(const RoundNum, const MsgTypes);

  const cs::PublicKey& getMyPublicKey() const {
    return myPublicKey_;
  }
  NodeLevel getMyLevel() const {
    return myLevel_;
  }
  uint32_t getRoundNumber();
  uint8_t getMyConfNumber();

  BlockChain& getBlockChain() {
    return bc_;
  }
  const BlockChain& getBlockChain() const {
    return bc_;
  }

#ifdef NODE_API
  csconnector::connector& getConnector() {
    return api_;
  }
#endif

private:
  bool init();

  // signature verification
  bool checkKeysFile();
  void generateKeys();
  bool checkKeysForSig();

  bool readRoundData(cs::RoundTable& roundTable);
  void onRoundStart(const cs::RoundTable& roundTable);

  void composeMessageWithBlock(const csdb::Pool&, const MsgTypes);
  void composeCompressed(const void*, const uint32_t, const MsgTypes);

  // Info
  const cs::PublicKey myPublicKey_;
  bool good_ = true;

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

  cs::Solver* solver_;
  Transport* transport_;

#ifdef MONITOR_NODE
  csstats::csstats       stats_;
#endif

#ifdef NODE_API
  csconnector::connector api_;
#endif

  RegionAllocator packStreamAllocator_;
  RegionAllocator allocator_;

  size_t lastStartSequence_;
  bool blocksReceivingStarted_ = false;

  IPackStream istream_;
  OPackStream ostream_;
};
#endif  // __NODE_HPP__
