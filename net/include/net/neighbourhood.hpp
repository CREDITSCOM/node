/* Send blaming letters to @yrtimd */
#ifndef __NEIGHBOURHOOD_HPP__
#define __NEIGHBOURHOOD_HPP__
#include <boost/asio.hpp>

#include <lib/system/allocators.hpp>
#include <lib/system/keys.hpp>

#include "packet.hpp"

using namespace boost::asio;

class Network;
class Transport;

class BlockChain;

const uint32_t MaxMessagesToKeep = 32;

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

  Connection() = default;
  Connection(Connection&& rhs): id(rhs.id),
                                lastBytesCount(rhs.lastBytesCount.load(std::memory_order_relaxed)),
                                lastPacketsCount(rhs.lastPacketsCount),
                                attempts(rhs.attempts),
                                key(rhs.key),
                                in(std::move(rhs.in)),
                                specialOut(rhs.specialOut),
                                out(std::move(rhs.out)),
                                node(std::move(rhs.node)),
                                isSignal(rhs.isSignal),
                                connected(rhs.connected),
                                msgRels(std::move(rhs.msgRels)) { }

  Connection(const Connection&) = delete;
  ~Connection() { }

  const ip::udp::endpoint& getOut() const { return specialOut ? out : in; }

  Id id = 0;

  static const uint32_t BytesLimit = 1 << 20;
  mutable std::atomic<uint32_t> lastBytesCount = { 0 };

  uint64_t lastPacketsCount = 0;
  uint32_t attempts = 0;

  PublicKey key;
  ip::udp::endpoint in;

  bool specialOut = false;
  ip::udp::endpoint out;

  RemoteNodePtr node;

  bool isSignal = 0;
  bool connected = false;

  struct MsgRel {
    uint32_t acceptOrder = 0;
    bool needSend = true;
  };
  FixedHashMap<Hash, MsgRel, uint16_t, MaxMessagesToKeep> msgRels;

  bool operator!=(const Connection& rhs) const {
    return id != rhs.id || key != rhs.key || in != rhs.in || specialOut != rhs.specialOut || (specialOut && out != rhs.out);
  }
};

typedef MemPtr<TypedSlot<Connection>> ConnectionPtr;

class Neighbourhood {
public:
  const static uint32_t MinConnections = 1;
  const static uint32_t MaxConnections = 1024;
  const static uint32_t MaxNeighbours = 32;

  const static uint32_t MaxConnectAttempts = 64;

  Neighbourhood(Transport*);

  void sendByNeighbours(const Packet*);

  void establishConnection(const ip::udp::endpoint&);
  void addSignalServer(const ip::udp::endpoint& in, const ip::udp::endpoint& out, RemoteNodePtr);

  void gotRegistration(Connection&&, RemoteNodePtr);

  void gotConfirmation(const Connection::Id& my,
                       const Connection::Id& real,
                       const ip::udp::endpoint&,
                       const PublicKey&,
                       RemoteNodePtr);

  void gotRefusal(const Connection::Id&);

  void resendPackets();
  void checkPending();
  void checkSilent();

  void refreshLimits();

  bool canHaveNewConnection();

  void neighbourHasPacket(RemoteNodePtr, const Hash&);
  void neighbourSentPacket(RemoteNodePtr, const Hash&);
  void neighbourSentRenounce(RemoteNodePtr, const Hash&);

  void redirectByNeighbours(const Packet*);
  void pourByNeighbours(const Packet*, const uint32_t packNum);

  void pingNeighbours();
  void validateConnectionId(RemoteNodePtr,
                            const Connection::Id,
                            const ip::udp::endpoint&);

  ConnectionPtr getNextRequestee(const Hash&);

private:
  struct BroadPackInfo {
    Packet pack;
    Connection::Id receivers[MaxNeighbours];
    Connection::Id* recEnd = receivers;
  };

  bool dispatchBroadcast(BroadPackInfo&);

  ConnectionPtr getConnection(const ip::udp::endpoint&);

  void connectNode(RemoteNodePtr, ConnectionPtr);
  void disconnectNode(ConnectionPtr*);

  Transport* transport_;

  TypedAllocator<Connection> connectionsAllocator_;

  std::atomic_flag nLockFlag_ = ATOMIC_FLAG_INIT;
  FixedVector<ConnectionPtr, MaxNeighbours> neighbours_;

  std::atomic_flag mLockFlag_ = ATOMIC_FLAG_INIT;
  FixedHashMap<ip::udp::endpoint,
               ConnectionPtr,
               uint16_t,
               MaxConnections> connections_;

  struct SenderInfo {
    uint32_t totalSenders = 0;
    uint32_t reaskTimes = 0;
    ConnectionPtr prioritySender;
  };

  FixedHashMap<Hash, SenderInfo, uint16_t, MaxMessagesToKeep> msgSenders_;
  FixedHashMap<Hash, BroadPackInfo, uint16_t, 10000> msgBroads_;
};

#endif // __NEIGHBOURHOOD_HPP__
