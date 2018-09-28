/* Send blaming letters to @yrtimd */
#include <sodium.h>

#include "neighbourhood.hpp"
#include "transport.hpp"

#include <csnode/blockchain.hpp>

Neighbourhood::Neighbourhood(Transport* net):
    transport_(net),
    connectionsAllocator_(MaxConnections + 1) {
  sodium_init();
}

template <typename T>
T getSecureRandom() {
  T result;
  randombytes_buf(static_cast<void*>(&result), sizeof(T));
  return result;
}

bool Neighbourhood::dispatchBroadcast(Neighbourhood::BroadPackInfo& bp) {
  bool result = false;
  if (!transport_->shouldSendPacket(bp.pack)) return result;

  uint32_t c = 0;
  for (auto& nb : neighbours_) {
    if (nb->isSignal) continue;
    bool found = false;
    for (auto ptr = bp.receivers; ptr != bp.recEnd; ++ptr) {
      if (*ptr == nb->id) {
        found = true;
        break;
      }
    }

    if (!found) {
      result = true;
      transport_->sendDirect(&(bp.pack), **nb);
    }
  }

  return result;
}

void Neighbourhood::sendByNeighbours(const Packet* pack) {
  SpinLock l(nLockFlag_);
  auto& bp = msgBroads_.tryStore(pack->getHash());
  if (!bp.pack) bp.pack = *pack;
  dispatchBroadcast(bp);
}

bool Neighbourhood::canHaveNewConnection() {
  SpinLock l(nLockFlag_);
  return neighbours_.size() < MaxNeighbours;
}

void Neighbourhood::checkPending() {
  SpinLock l1(mLockFlag_);
  LOG_DEBUG("CONNECTIONS: ");
  // If the connection cannot be established, retry it
  for (auto conn = connections_.begin();
       conn != connections_.end();
       ++conn) {
    // Attempt to reconnect if the connection hasn't been established yet
    if (!(**conn)->connected && (**conn)->attempts < MaxConnectAttempts)
      transport_->sendRegistrationRequest(****conn);

  }
  for (auto conn = connections_.begin();
       conn != connections_.end();
       ++conn) {
    LOG_DEBUG((conn->data)->id << ". " << (conn->data).get() << ": " << (conn->data)->in << " : " << (conn->data)->out << " ~ " << (conn->data)->specialOut << " ~ " << (conn->data)->connected << " ~ " << (conn->data)->node.get());
  }

  SpinLock l2(nLockFlag_);
  LOG_DEBUG("NEIGHBOURS: ");
  for (auto conn = neighbours_.begin(); conn != neighbours_.end(); ++conn)
    LOG_DEBUG(conn->get() << " : " << (*conn)->in << " : " << (*conn)->getOut() << " : " << (*conn)->id << " ~ " << (bool)(*conn)->node);
}

void Neighbourhood::refreshLimits() {
  SpinLock l(nLockFlag_);
  for (auto conn = neighbours_.begin(); conn != neighbours_.end(); ++conn)
    (*conn)->lastBytesCount.store(0, std::memory_order_relaxed);
}

void Neighbourhood::checkSilent() {
  bool needRefill = true;

  {
    SpinLock lm(mLockFlag_);
    SpinLock ln(nLockFlag_);

    for (auto conn = neighbours_.begin();
         conn != neighbours_.end();
         ++conn) {
      if ((*conn)->isSignal)
        continue;

      if (!(*conn)->node) {
        ConnectionPtr tc = *conn; 
        disconnectNode(conn);
        --conn;
        continue;
      }

      const auto packetsCount = (*(*conn)->node)->packets.
        load(std::memory_order_relaxed);

      if (packetsCount == (*conn)->lastPacketsCount) {
        LOG_WARN("Node " << (*conn)->in << " stopped responding");

        ConnectionPtr tc = *conn;
        Connection* c = *tc;
        tc->node->connection.compare_exchange_strong(c,
          nullptr,
          std::memory_order_release,
          std::memory_order_relaxed);

        disconnectNode(conn);
        --conn;
      }
      else {
        needRefill = false;
        (*conn)->lastPacketsCount = packetsCount;
      }
    }
  }

  //if (needRefill)
    //transport_->refillNeighbourhood();
}

template <typename Vec>
static ConnectionPtr* findInVec(const Connection::Id& id, Vec& vec) {
  for (auto it = vec.begin(); it != vec.end(); ++it)
    if ((*it)->id == id)
      return it;

  return nullptr;
}

template <typename Vec>
static ConnectionPtr* findInMap(const Connection::Id& id, Vec& vec) {
  for (auto it = vec.begin(); it != vec.end(); ++it)
    if (it->data->id == id)
      return &(it->data);

  return nullptr;
}

static ip::udp::endpoint getIndexingEndpoint(const ip::udp::endpoint& ep) {
  if (ep.address().is_v6()) return ep;
  return ip::udp::endpoint(ip::make_address_v6(ip::v4_mapped, ep.address().to_v4()),
                           ep.port());
}

ConnectionPtr Neighbourhood::getConnection(const ip::udp::endpoint& ep) {
  LOG_WARN("Getting connection");
  auto& conn = connections_.tryStore(getIndexingEndpoint(ep));

  if (!conn) {
    conn = connectionsAllocator_.emplace();
    conn->in = ep;
  }

  return conn;
}

void Neighbourhood::establishConnection(const ip::udp::endpoint& ep) {
  LOG_WARN("Establishing connection to " << ep);
  SpinLock lp(mLockFlag_);

  auto conn = getConnection(ep);
  if (!conn->id)
    conn->id = getSecureRandom<Connection::Id>();

  transport_->sendRegistrationRequest(**conn);
}

void Neighbourhood::addSignalServer(const ip::udp::endpoint& in,
                                    const ip::udp::endpoint& out,
                                    RemoteNodePtr node) {
  SpinLock lp(mLockFlag_);
  SpinLock ln(nLockFlag_);

  if ((*node)->connection.load(std::memory_order_relaxed)) {
    LOG_ERROR("Connection with the SS node has already been established");
    return;
  }

  ConnectionPtr conn = getConnection(out);
  if (!conn->id)
    conn->id = getSecureRandom<Connection::Id>();

  conn->in = in;
  if (in != out) {
    conn->specialOut = true;
    conn->out = out;
  }

  conn->isSignal = true;
  connectNode(node, conn);
}

/* Assuming both the mutexes have been locked */
void Neighbourhood::connectNode(RemoteNodePtr node,
                                ConnectionPtr conn) {
  Connection* connection = nullptr;
  while (!node->connection.compare_exchange_strong(connection,
                                                   *conn,
                                                   std::memory_order_release,
                                                   std::memory_order_relaxed));

  if (connection) {
    auto connPtr = findInVec(connection->id, neighbours_);
    if (connPtr) disconnectNode(connPtr);
  }

  conn->node = node;

  if (conn->connected) return;
  conn->connected = true;
  neighbours_.emplace(conn);
}

void Neighbourhood::disconnectNode(ConnectionPtr* connPtr) {
  (*connPtr)->connected = false;
  (*connPtr)->node = RemoteNodePtr();
  neighbours_.remove(connPtr);
}

void Neighbourhood::gotRegistration(Connection&& conn,
                                    RemoteNodePtr node) {
  SpinLock l1(mLockFlag_);
  SpinLock l2(nLockFlag_);

  ConnectionPtr& connPtr = connections_.tryStore(getIndexingEndpoint(conn.getOut()));
  if (!connPtr)
    connPtr = connectionsAllocator_.emplace(std::move(conn));
  else {
    if (conn.id < connPtr->id)
      connPtr->id = conn.id;
    connPtr->key = conn.key;

    connPtr->in = conn.in;
    connPtr->specialOut = conn.specialOut;
    connPtr->out = conn.out;
  }

  connectNode(node, connPtr);
  transport_->sendRegistrationConfirmation(**connPtr, conn.id);
}

void Neighbourhood::gotConfirmation(const Connection::Id& my,
                                    const Connection::Id& real,
                                    const ip::udp::endpoint& ep,
                                    const PublicKey& pk,
                                    RemoteNodePtr node) {
  SpinLock l1(mLockFlag_);
  SpinLock l2(nLockFlag_);

  ConnectionPtr* connPtr = findInMap(my, connections_);
  if (!connPtr) {
    LOG_WARN("Connection with ID " << my << " not found");
    return;
  }

  if (ep != (*connPtr)->in) {
    (*connPtr)->out = (*connPtr)->in;
    (*connPtr)->specialOut = true;
    (*connPtr)->in = ep;
  }

  if (my != real) (*connPtr)->id = real;

  connectNode(node, *connPtr);
}

void Neighbourhood::validateConnectionId(RemoteNodePtr node,
                                         const Connection::Id id,
                                         const ip::udp::endpoint& ep) {
  SpinLock l1(mLockFlag_);
  SpinLock l2(nLockFlag_);

  auto realPtr = findInMap(id, connections_);
  if (!realPtr) {
    //LOG_WARN("Validation: Connection " << id << " not found");
    return;
  }
  else if (realPtr->get() != node->connection.load(std::memory_order_relaxed)) {
    if (!(*realPtr)->specialOut && (*realPtr)->in != ep) {
      (*realPtr)->specialOut = true;
      (*realPtr)->out = (*realPtr)->in;
    }
    (*realPtr)->in = ep;
    connectNode(node, *realPtr);
  }
}

void Neighbourhood::gotRefusal(const Connection::Id& id) {}

void Neighbourhood::neighbourHasPacket(RemoteNodePtr node,
                                       const Hash& hash) {
  SpinLock l(nLockFlag_);
  auto conn = node->connection.load(std::memory_order_relaxed);
  if (!conn) return;

  auto& bp = msgBroads_.tryStore(hash);
  for (auto ptr = bp.receivers; ptr != bp.recEnd; ++ptr) {
    if (*ptr == conn->id) return;
  }

  if ((bp.recEnd - bp.receivers) < MaxNeighbours)
    *(bp.recEnd++) = conn->id;
}

void Neighbourhood::neighbourSentPacket(RemoteNodePtr node,
                                        const Hash& hash) {
  SpinLock l(nLockFlag_);
  auto connection = node->connection.load(std::memory_order_acquire);
  if (!connection) return;

  Connection::MsgRel& rel = connection->msgRels.tryStore(hash);
  SenderInfo& sInfo = msgSenders_.tryStore(hash);

  rel.needSend = false;

  if (!sInfo.prioritySender) {
    // First time
    auto connPtr = findInVec(connection->id, neighbours_);
    if (connPtr) {
      sInfo.prioritySender = *connPtr;

      rel.acceptOrder = sInfo.totalSenders++;

      for (auto& nb : neighbours_)
        if (nb->id != connection->id)
          transport_->sendPackRenounce(hash, **nb);
    }
  }
  else if (*sInfo.prioritySender != connection) {
    if (!rel.acceptOrder) rel.acceptOrder = ++sInfo.totalSenders;
    transport_->sendPackRenounce(hash, *connection);
  }
}

void Neighbourhood::neighbourSentRenounce(RemoteNodePtr node,
                                          const Hash& hash) {
  SpinLock l(nLockFlag_);
  auto connection = node->connection.load(std::memory_order_acquire);
  if (connection) {
    SenderInfo& si = msgSenders_.tryStore(hash);
    Connection::MsgRel& rel = connection->msgRels.tryStore(hash);
    rel.acceptOrder = si.totalSenders++;
    rel.needSend = false;
  }
}

void Neighbourhood::redirectByNeighbours(const Packet* pack) {
  SpinLock l(nLockFlag_);
  for (auto& nb : neighbours_) {
    Connection::MsgRel& rel = nb->msgRels.tryStore(pack->getHeaderHash());
    if (rel.needSend) {
      transport_->sendDirect(pack, **nb);
    }
  }
}

void Neighbourhood::pourByNeighbours(const Packet* pack,
                                     const uint32_t packNum) {
  if (packNum <= Packet::SmartRedirectTreshold) {
    const auto end = pack + packNum;
    for (auto ptr = pack; ptr != end; ++ptr) {
      sendByNeighbours(ptr);
    }

    return;
  }

  {
    SpinLock l(nLockFlag_);
    for (auto& nb : neighbours_)
      transport_->sendPackRenounce(pack->getHeaderHash(), **nb);
  }

  ConnectionPtr* conn;
  static uint32_t i = 0;
  uint32_t tr = 0;
  const Packet* packEnd = pack + packNum;

  for (;;) {
    {
      SpinLock l(nLockFlag_);
      if (i >= neighbours_.size()) i = 0;
      conn = neighbours_.begin() + i;
      ++i;
    }

    Connection::MsgRel& rel = (*conn)->msgRels.tryStore(pack->getHeaderHash());
    if (!rel.needSend) continue;

    for (auto p = pack; p != packEnd; ++p)
      transport_->sendDirect(p, ***conn);

    if (++tr == 2) break;
  }
}

void Neighbourhood::pingNeighbours() {
  SpinLock l(nLockFlag_);
  for (auto& nb : neighbours_)
    transport_->sendPingPack(**nb);
}

void Neighbourhood::resendPackets() {
  SpinLock l(nLockFlag_);
  uint32_t cnt = 0;
  for (auto& bp : msgBroads_) {
    if (!bp.data.pack) continue;
    if (!dispatchBroadcast(bp.data))
      bp.data.pack = Packet();
    else
      ++cnt;
  }

  /LOG_DEBUG("TPTR: " << cnt);
}

ConnectionPtr Neighbourhood::getNextRequestee(const Hash& hash) {
  SpinLock l(nLockFlag_);

  SenderInfo& si = msgSenders_.tryStore(hash);
  ++si.reaskTimes;

  if (si.totalSenders < si.reaskTimes) {
    si.reaskTimes = 0;
    return si.prioritySender;
  }

  for (auto& nb : neighbours_) {
    if (nb->isSignal) continue;
    Connection::MsgRel& rel = nb->msgRels.tryStore(hash);
    if (rel.acceptOrder == si.reaskTimes) return nb;
  }

  return si.prioritySender;
}
