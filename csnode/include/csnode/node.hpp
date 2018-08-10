/* Send blaming letters to @yrtimd */
#ifndef __NODE_HPP__
#define __NODE_HPP__
#include <memory>

#include <csstats.h>
#include <csconnector/csconnector.h>

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
namespace Credits { class ISolver; }

class Node {
public:
  Node(const Config&);
  ~Node();

  bool isGood() const { return good_; }
  void run(const Config&);

  /* Incoming requests processing */
  void getInitRing(const uint8_t*, const size_t);
  void getRoundTable(const uint8_t*, const size_t, const RoundNum);
  void getTransaction(const uint8_t*, const size_t);
  void getFirstTransaction(const uint8_t*, const size_t);
  void getTransactionsList(const uint8_t*, const size_t);
  void getVector(const uint8_t*, const size_t, const PublicKey& sender);
  void getMatrix(const uint8_t*, const size_t, const PublicKey& sender);
  void getBlock(const uint8_t*, const size_t, const PublicKey& sender);
  void getHash(const uint8_t*, const size_t, const PublicKey& sender);

  /* Outcoming requests forming */
  void sendRoundTable();
  void sendTransaction(const csdb::Transaction&);
  void sendTransaction(std::vector<csdb::Transaction>&&);
  void sendFirstTransaction(const csdb::Transaction&);
  void sendTransactionList(const csdb::Pool&, const PublicKey&);
  void sendVector(const Vector&);
  void sendMatrix(const Matrix&);
  void sendBlock(const csdb::Pool&);
  void sendHash(const Hash&, const PublicKey&);

  void flushCurrentTasks();
  void becomeWriter();
  void initNextRound(const PublicKey& mainNode, std::vector<PublicKey>&& confidantNodes);

  enum MessageActions {
    Process,
    Postpone,
    Drop
  };
  MessageActions chooseMessageAction(const RoundNum, const MsgTypes);

  const PublicKey& getMyPublicKey() const { return myPublicKey_; }
  NodeLevel getMyLevel() const { return myLevel_; }

  const std::vector<PublicKey>& getConfidants() const { return confidantNodes_; }

  BlockChain& getBlockChain() { return bc_; }
  const BlockChain& getBlockChain() const { return bc_; }

  csconnector::csconnector& getConnector() { return api_; }

  PublicKey writerId;

private:
  bool init();

  inline bool readRoundData(bool);
  void onRoundStart();

  // Info
  const PublicKey myPublicKey_;
  bool good_ = true;

  // Current round state
  RoundNum roundNum_ = 0;
  NodeLevel myLevel_;

  PublicKey mainNode_;
  std::vector<PublicKey> confidantNodes_;

  // Resources
  BlockChain bc_;

  Credits::ISolver* solver_;
  Transport* transport_;

  csstats::csstats stats_;
  csconnector::csconnector api_;

  RegionAllocator allocator_;

  IPackStream istream_;
  OPackStream ostream_;
};

#endif // __NODE_HPP__
