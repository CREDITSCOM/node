#ifndef __NEIGHBOURHOOD_HPP__
#define __NEIGHBOURHOOD_HPP__
#include <boost/asio.hpp>

#include <lib/system/allocators.hpp>
#include <lib/system/keys.hpp>

using namespace boost::asio;

class Network;
class Transport;
class Packet;

struct Connection;
struct RemoteNode {
  std::atomic<uint64_t> packets = { 0 };

  std::atomic<uint32_t> strikes = { 0 };
  std::atomic<bool> blackListed = { false };

  void addStrike() {
    strikes.fetch_add(1, std::memory_order_relaxed);
  }

  bool isBlackListed() {
    return blackListed.load(std::memory_order_relaxed);
  }

  std::atomic<Connection*> connection = { nullptr };
};

typedef MemPtr<TypedSlot<RemoteNode>> RemoteNodePtr;

struct Connection {
  typedef uint64_t Id;

  Id id;

  uint64_t lastPacketsCount = 0;
  uint32_t attempts = 0;

  PublicKey key;
  ip::udp::endpoint in;

  bool specialOut = false;
  ip::udp::endpoint out;

  RemoteNodePtr node;

  bool operator!=(const Connection& rhs) const {
    return id != rhs.id || key != rhs.key || in != rhs.in || specialOut != rhs.specialOut || (specialOut && out != rhs.out);
  }
};

typedef MemPtr<TypedSlot<Connection>> ConnectionPtr;

class Neighbourhood {
public:
  const static uint32_t MaxConnections = 64;
  const static uint32_t MaxConnectAttempts = 64;

  Neighbourhood(Transport*);

  void sendByNeighbours(const Packet*);

  void establishConnection(const ip::udp::endpoint&);
  void addSignalServer(const ip::udp::endpoint&);

  void gotRegistration(Connection&&, RemoteNodePtr);

  void gotConfirmation(const Connection::Id&,
                       const ip::udp::endpoint&,
                       const PublicKey&,
                       RemoteNodePtr);

  void gotRefusal(const Connection::Id&);

  void connectNode(RemoteNodePtr, ConnectionPtr);

  void checkPending();
  void checkSilent();

private:
  Transport* transport_;

  TypedAllocator<Connection> connectionsAllocator_;

  std::atomic_flag nLockFlag_ = ATOMIC_FLAG_INIT;
  FixedVector<ConnectionPtr, MaxConnections> neighbours_;

  std::atomic_flag pLockFlag_ = ATOMIC_FLAG_INIT;
  FixedVector<ConnectionPtr, MaxConnections> pendingConnections_;
};

#endif // __NEIGHBOURHOOD_HPP__
