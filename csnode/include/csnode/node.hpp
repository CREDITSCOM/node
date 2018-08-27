/* Send blaming letters to @yrtimd */
#ifndef __NODE_HPP__
#define __NODE_HPP__
#include <memory>

#include <csstats.h>
#include <csconnector/csconnector.h>
#include <Solver/Fake/Solver.hpp>
#include <client/config.hpp>

#include "blockchain.hpp"
#include "packstream.hpp"

enum NodeLevel {
  Normal,
  Confidant,
  Main,
  Writer
};

typedef std::string Vector;
typedef std::string Matrix;

class Transport;
namespace Credits { class Solver; }

class Node {
public:
  Node(const Config&);
  ~Node();

  bool isGood() const { return good_; }
  void run(const Config&);

  /* Incoming requests processing */
  void getInitRing(const uint8_t*, const size_t);
  void getRoundTable(const uint8_t*, const size_t, const RoundNum);
  void getBigBang(const uint8_t*, const size_t, const RoundNum);
  void getTransaction(const uint8_t*, const size_t);
  void getFirstTransaction(const uint8_t*, const size_t);
  void getTransactionsList(const uint8_t*, const size_t);
  void getVector(const uint8_t*, const size_t, const PublicKey& sender);
  void getMatrix(const uint8_t*, const size_t, const PublicKey& sender);
  void getBlock(const uint8_t*, const size_t, const PublicKey& sender);
  void getHash(const uint8_t*, const size_t, const PublicKey& sender);
  /*syncro get functions*/
  void getBlockRequest(const uint8_t*, const size_t, const PublicKey& sender);
  void getBlockReply(const uint8_t*, const size_t);
  void getTLConfirmation(const uint8_t* data, const size_t size);

  /* Outcoming requests forming */
  void sendRoundTable();
  void sendTransaction(const csdb::Transaction&);
  void sendTransaction(std::vector<csdb::Transaction>&&);
  void sendFirstTransaction(const csdb::Transaction&);
  void sendTransactionList(const csdb::Pool&, const PublicKey&);
  void sendVector(const Credits::HashVector&);
  void sendMatrix(const Credits::HashMatrix&);
  void sendBlock(const csdb::Pool&);
  void sendHash(const Hash&, const PublicKey&);
  /*syncro send functions*/
  void sendBlockRequest(uint32_t seq);
  void sendBlockReply(const csdb::Pool&, const PublicKey&);

  void sendVectorRequest(const PublicKey&);
  void sendMatrixRequest(const PublicKey&);

  void getVectorRequest(const uint8_t* data, const size_t size);
  void getMatrixRequest(const uint8_t* data, const size_t size);

  void flushCurrentTasks();
  void becomeWriter();
  void initNextRound(const PublicKey& mainNode, std::vector<PublicKey>&& confidantNodes);
  void sendTLConfirmation(size_t tcount);

  enum MessageActions {
    Process,
    Postpone,
    Drop
  };
  MessageActions chooseMessageAction(const RoundNum, const MsgTypes);

  const PublicKey& getMyPublicKey() const { return myPublicKey_; }
  NodeLevel getMyLevel() const { return myLevel_; }
  uint32_t getRoundNumber();
  //bool getSyncroStarted();
  uint8_t getMyConfNumber();

  const std::vector<PublicKey>& getConfidants() const { return confidantNodes_; }

  BlockChain& getBlockChain() { return bc_; }
  const BlockChain& getBlockChain() const { return bc_; }

  csconnector::csconnector& getConnector() { return api_; }

  PublicKey writerId;

private:
  bool init();

  //signature verification
  bool checkKeysFile();
  void generateKeys();
  bool checkKeysForSig();

  inline bool readRoundData(bool);
  void onRoundStart();

  // Info
  const PublicKey myPublicKey_;
  bool good_ = true;

  //syncro variables
  bool syncro_started = false;
  uint32_t sendBlockRequestSequence;
  bool awaitingSyncroBlock;
  uint32_t awaitingRecBlockCount = 0;




  //signature variables
  std::vector<uint8_t> myPublicForSig;
  std::vector<uint8_t> myPrivateForSig;



  // Current round state
  RoundNum roundNum_ = 0;
  NodeLevel myLevel_;

  PublicKey mainNode_;
  std::vector<PublicKey> confidantNodes_;

  uint8_t myConfNumber;

  // Resources
  BlockChain bc_;

  Credits::Solver* solver_;
  Transport* transport_;

  csstats::csstats stats_;
  csconnector::csconnector api_;

  RegionAllocator allocator_;

  IPackStream istream_;
  OPackStream ostream_;
};

#endif // __NODE_HPP__
