/* Send blaming letters to @yrtimd */
#ifndef __TRANSPORT_HPP__
#define __TRANSPORT_HPP__
#include <boost/asio.hpp>

#include <lib/system/allocators.hpp>
#include <lib/system/keys.hpp>
#include <lib/system/logger.hpp>
#include <client/config.hpp>
#include <csnode/packstream.hpp>

#include "neighbourhood.hpp"
#include "pacmans.hpp"

using namespace boost::asio;

typedef uint64_t ConnectionId;
typedef uint64_t Tick;

enum class NetworkCommand: uint8_t {
  Registration,
  ConfirmationRequest,
  ConfirmationResponse,
  RegistrationConfirmed,
  RegistrationRefused,
  Ping,
  Pong
};

enum class RegistrationRefuseReasons: uint8_t {
  Unspecified,
  LimitReached,
  TooManyQueries,
  BadClientVersion,
  Timeout,
  BadResponse
};

struct RemoteNode {
  enum Status: uint8_t {
    New,
    BlackListed,
    AwaitingRegistration,
    IsNeighbour
  };

  std::atomic<Status> status = { Status::New };
  std::atomic<uint32_t> strikes = { 0 };

  void addStrike() {
    strikes.fetch_add(1, std::memory_order_relaxed);
  }

  Tick regTick;
  Neighbourhood::Element* neighbour;
};

template <>
uint16_t getHashIndex(const ip::udp::endpoint&);

class Transport {
public:
  static Transport* init(Network*);
  void run(const Config& config);

  RemoteNode& getPackSenderEntry(const ip::udp::endpoint&);

  void processNetworkTask(const TaskPtr<IPacMan>&, RemoteNode&);

  void processNodeMessage(const Message&);
  void processNodeMessage(const Packet&);

  const Neighbourhood& getNeighbourhood() const { return nh_; }

  void sendDirect(const Packet* pack, const NeighbourEndpoints&);
  void sendBroadcast(const Packet* pack);

  const PublicKey& getMyPublicKey() const { return myPublicKey_; }

private:
  Transport(Network* net,
            const PublicKey& myPublicKey):
    netPacksAllocator_(1 << 24, 1),
    myPublicKey_(myPublicKey),
    oPackStream_(&netPacksAllocator_, myPublicKey),
    net_(net) { }

  // Dealing with network connections
  void refuseRegistration(RemoteNode&, const RegistrationRefuseReasons);
  void confirmRegistration(RemoteNode&);

  void dispatchNodeMessage(const Packet& firstPack,
                           const uint8_t* data,
                           const size_t);

  static Transport* transportPtr_;

  static const uint32_t MaxPacksQueue = 2048;
  static const uint32_t MaxRemoteNodes = 4096;
  static const uint32_t MaxConnectionRequests = 32;

  FixedCircularBuffer<PacketPtr,
                      MaxPacksQueue> sendPacks_;

  FixedHashMap<ip::udp::endpoint,
               RemoteNode,
               uint16_t,
               MaxRemoteNodes> remoteNodes_;

  RegionAllocator netPacksAllocator_;
  PublicKey myPublicKey_;

  IPackStream iPackStream_;
  OPackStream oPackStream_;

  // Registration data
  Packet regPack_;
  uint64_t* regPackConnId_;

  Neighbourhood::Element* connections_[MaxConnectionRequests];
  Neighbourhood::Element** connectionsEnd_ = connections_;

  bool acceptRegistrations_ = false;

  Network* net_;
  Neighbourhood nh_;
};

#endif // __TRANSPORT_HPP__
