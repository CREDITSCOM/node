/* Send blaming letters to @yrtimd */
#ifndef __TRANSPORT_HPP__
#define __TRANSPORT_HPP__
#include <boost/asio.hpp>

#include <lib/system/allocators.hpp>
#include <lib/system/keys.hpp>
#include <lib/system/logger.hpp>
#include <net/network.hpp>
#include <client/config.hpp>
#include <csnode/node.hpp>
#include <csnode/packstream.hpp>

#include "neighbourhood.hpp"
#include "pacmans.hpp"

using namespace boost::asio;

typedef uint64_t ConnectionId;
typedef uint64_t Tick;

enum class NetworkCommand: uint8_t {
  Registration = 2,
  ConfirmationRequest,
  ConfirmationResponse,
  RegistrationConfirmed,
  RegistrationRefused,
  Ping,
  SSRegistration = 1,
  SSFirstRound = 31,
  SSRegistrationRefused = 25,
  SSPingWhiteNode = 32
};

enum class RegistrationRefuseReasons: uint8_t {
  Unspecified,
  LimitReached,
  BadId,
  BadClientVersion,
  Timeout,
  BadResponse
};

enum class SSBootstrapStatus: uint8_t {
  Empty,
  Requested,
  RegisteredWait,
  Complete,
  Denied
};

template <>
uint16_t getHashIndex(const ip::udp::endpoint&);

class Transport {
public:
  Transport(const Config& config, Node* node):
    config_(config),
    remoteNodes_(MaxRemoteNodes + 1),
    netPacksAllocator_(1 << 24, 1),
    myPublicKey_(node->getMyPublicKey()),
    oPackStream_(&netPacksAllocator_, node->getMyPublicKey()),
    net_(new Network(config, this)),
    node_(node),
    nh_(this) {
    good_ = net_->isGood();
  }

  ~Transport() {
    delete net_;
  }

  void run();

  RemoteNodePtr getPackSenderEntry(const ip::udp::endpoint&);

  void processNetworkTask(const TaskPtr<IPacMan>&,
                          RemoteNodePtr&);
  void processNodeMessage(const Message&);
  void processNodeMessage(const Packet&);

  void addTask(Packet*, const uint32_t packNum);
  void clearTasks();

  const PublicKey& getMyPublicKey() const { return myPublicKey_; }
  bool isGood() const { return good_; }

  void sendBroadcast(const Packet* pack) {
    nh_.sendByNeighbours(pack);
  }

  void sendDirect(const Packet* pack,
                  const Connection& conn) {
    net_->sendDirect(*pack,
                     conn.specialOut ?
                     conn.out : conn.in);
  }

  void refillNeighbourhood();
  void processPostponed(const RoundNum) { }

  void sendRegistrationRequest(Connection&);
  void sendRegistrationConfirmation(const Connection&);
  void sendRegistrationRefusal(const Connection&, const RegistrationRefuseReasons);

private:
  // Dealing with network connections
  bool parseSSSignal(const TaskPtr<IPacMan>&);

  void dispatchNodeMessage(const Packet& firstPack,
                           const uint8_t* data,
                           size_t);

  /* Network packages processing */
  bool gotRegistrationRequest(const TaskPtr<IPacMan>&,
                              RemoteNodePtr&);

  bool gotRegistrationConfirmation(const TaskPtr<IPacMan>&,
                                   RemoteNodePtr&);

  bool gotRegistrationRefusal(const TaskPtr<IPacMan>&,
                              RemoteNodePtr&);

  bool gotSSRegistration(const TaskPtr<IPacMan>&, RemoteNodePtr&);
  bool gotSSRefusal(const TaskPtr<IPacMan>&);
  bool gotSSDispatch(const TaskPtr<IPacMan>&);
  bool gotSSPingWhiteNode(const TaskPtr<IPacMan>&);

  /* Actions */
  bool good_;
  Config config_;

  static const uint32_t MaxPacksQueue = 2048;
  static const uint32_t MaxRemoteNodes = 4096;

  std::atomic_flag sendPacksFlag_ = ATOMIC_FLAG_INIT;
  FixedCircularBuffer<Packet,
                      MaxPacksQueue> sendPacks_;

  TypedAllocator<RemoteNode> remoteNodes_;

  FixedHashMap<ip::udp::endpoint,
               RemoteNodePtr,
               uint16_t,
               MaxRemoteNodes,
               true> remoteNodesMap_;

  RegionAllocator netPacksAllocator_;
  PublicKey myPublicKey_;

  IPackStream iPackStream_;
  OPackStream oPackStream_;

  // SS Data
  SSBootstrapStatus ssStatus_ = SSBootstrapStatus::Empty;
  ip::udp::endpoint ssEp_;

  // Registration data
  Packet regPack_;
  uint64_t* regPackConnId_;
  bool acceptRegistrations_ = false;

  Network* net_;
  Node* node_;

  Neighbourhood nh_;
};

#endif // __TRANSPORT_HPP__
