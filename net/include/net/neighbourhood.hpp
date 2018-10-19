/* Send blaming letters to @yrtimd */
#ifndef __NEIGHBOURHOOD_HPP__
#define __NEIGHBOURHOOD_HPP__
#include <boost/asio.hpp>

#include <lib/system/allocators.hpp>
#include <lib/system/cache.hpp>
#include <lib/system/keys.hpp>
#include <lib/system/common.hpp>

#include "packet.hpp"

using namespace boost::asio;

class Network;
class Transport;

class BlockChain;

const uint32_t MaxMessagesToKeep = 128;
const uint32_t MaxResendTimes = 32;
const uint32_t MaxSyncAttempts = 8;

struct Connection;
struct RemoteNode {
  __cacheline_aligned std::atomic<uint64_t> packets = { 0 };

  __cacheline_aligned std::atomic<uint32_t> strikes = { 0 };
  __cacheline_aligned std::atomic<bool> blackListed = { false };

  void addStrike() {
    strikes.fetch_add(1, std::memory_order_relaxed);
  }

  bool isBlackListed() {
    return blackListed.load(std::memory_order_relaxed);
  }

  __cacheline_aligned std::atomic<Connection*> connection = { nullptr };
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

  cs::PublicKey key;
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
  FixedHashMap<cs::Hash, MsgRel, uint16_t, MaxMessagesToKeep> msgRels;

  uint32_t syncSeq = 0;
  uint32_t syncSeqRetries = 0;

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
                       const cs::PublicKey&,
                       RemoteNodePtr);

  void gotRefusal(const Connection::Id&);

  void resendPackets();
  void checkPending(const uint32_t maxNeighbours);
  void checkSilent();

  void refreshLimits();

  bool canHaveNewConnection();

  void neighbourHasPacket(RemoteNodePtr, const cs::Hash&, const bool isDirect);
  void neighbourSentPacket(RemoteNodePtr, const cs::Hash&);
  void neighbourSentRenounce(RemoteNodePtr, const cs::Hash&);

  void redirectByNeighbours(const Packet*);
  void pourByNeighbours(const Packet*, const uint32_t packNum);

  uint32_t size();

  void pingNeighbours();
  void validateConnectionId(RemoteNodePtr,
                            const Connection::Id,
                            const ip::udp::endpoint&);

  ConnectionPtr getConnection(const RemoteNodePtr);

  ConnectionPtr getNextRequestee(const cs::Hash&);
  ConnectionPtr getNextSyncRequestee(const uint32_t seq, bool& alreadyRequested);
  ConnectionPtr getNeighbourByKey(const cs::PublicKey&);

  void releaseSyncRequestee(const uint32_t seq);

  void registerDirect(const Packet*, ConnectionPtr);

private:
  struct BroadPackInfo {
    Packet pack;

    uint32_t attempts = 0;
    bool sentLastTime = false;

    Connection::Id receivers[MaxNeighbours];
    Connection::Id* recEnd = receivers;
  };

  struct DirectPackInfo {
    Packet pack;

    ConnectionPtr receiver;
    bool received = false;

    uint32_t attempts = 0;
  };

  bool dispatch(BroadPackInfo&, const bool force);
  bool dispatch(DirectPackInfo&);

  ConnectionPtr getConnection(const ip::udp::endpoint&);

  void connectNode(RemoteNodePtr, ConnectionPtr);
  void disconnectNode(ConnectionPtr*);

  Transport* transport_;

  TypedAllocator<Connection> connectionsAllocator_;

  __cacheline_aligned std::atomic_flag nLockFlag_ = ATOMIC_FLAG_INIT;
  FixedVector<ConnectionPtr, MaxNeighbours> neighbours_;

  __cacheline_aligned std::atomic_flag mLockFlag_ = ATOMIC_FLAG_INIT;
  FixedHashMap<ip::udp::endpoint,
               ConnectionPtr,
               uint16_t,
               MaxConnections> connections_;

  struct SenderInfo {
    uint32_t totalSenders = 0;
    uint32_t reaskTimes = 0;
    ConnectionPtr prioritySender;
  };

  FixedHashMap<cs::Hash, SenderInfo, uint16_t, MaxMessagesToKeep> msgSenders_;
  FixedHashMap<cs::Hash, BroadPackInfo, uint16_t, 10000> msgBroads_;
  FixedHashMap<cs::Hash, DirectPackInfo, uint16_t, 10000> msgDirects_;
};

#endif // __NEIGHBOURHOOD_HPP__
