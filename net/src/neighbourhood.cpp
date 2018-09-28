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

void Neighbourhood::sendByNeighbours(const Packet* pack) {
  SpinLock l(nLockFlag_);
  for (auto& nb : neighbours_)
    transport_->sendDirect(pack, **nb);
}

bool Neighbourhood::canHaveNewConnection() {
  SpinLock l(nLockFlag_);
  SpinLock ll(pLockFlag_);
  return !((neighbours_.size() + pendingConnections_.size()) >= MaxNeighbours);
}

void Neighbourhood::checkPending() {
  SpinLock l(pLockFlag_);
  // If the connection cannot be established, retry it
  for (auto conn = pendingConnections_.begin();
       conn != pendingConnections_.end();
       ++conn) {
    // Attempt to reconnect if the connection hasn't been established yet
    if (((*conn)->node &&
         (*conn)->node->connection.load(std::memory_order_relaxed) != **conn) ||
        ((*conn)->attempts >= Neighbourhood::MaxConnectAttempts)) {
      pendingConnections_.remove(conn);
      --conn;
    }
    else
      transport_->sendRegistrationRequest(***conn);
  }
}

void Neighbourhood::checkSilent() {
  bool needRefill = false;
  {
    SpinLock l(nLockFlag_);
    for (auto conn = neighbours_.begin();
         conn != neighbours_.end();
         ++conn) {
      if ((*conn)->isSignal) continue;

      const auto packetsCount = (*(*conn)->node)->packets.
        load(std::memory_order_relaxed);

      if (packetsCount == (*conn)->lastPacketsCount) {
        LOG_WARN("Node " << (*conn)->in << " stopped responding");

        ConnectionPtr tc = *conn;
        tc->node->connection.store(nullptr);
        neighbours_.remove(conn);

        --conn;

        {
          SpinLock ll(pLockFlag_);
          pendingConnections_.emplace(tc);
        }
      }
      else
        (*conn)->lastPacketsCount = packetsCount;
    }

    SpinLock ll(pLockFlag_);
    needRefill = ((neighbours_.size() + pendingConnections_.size()) < MinConnections);
  }

  if (needRefill) transport_->refillNeighbourhood();
}

void Neighbourhood::establishConnection(const ip::udp::endpoint& ep) {
  SpinLock ll(pLockFlag_);
  ConnectionPtr &conn =
    pendingConnections_.emplace(connectionsAllocator_.emplace());

  conn->id = getSecureRandom<Connection::Id>();
  conn->in = ep;

  transport_->sendRegistrationRequest(**conn);
}

void Neighbourhood::addSignalServer(const ip::udp::endpoint& in, const ip::udp::endpoint& out, RemoteNodePtr node) {
  SpinLock l(nLockFlag_);
  if ((*node)->connection.load(std::memory_order_relaxed)) return;

  ConnectionPtr conn = connectionsAllocator_.emplace();

  conn->id = getSecureRandom<Connection::Id>();
  conn->in = in;
  if (in != out) {
    conn->specialOut = true;
    conn->out = out;
  }

  conn->isSignal = true;

  conn->node = node;
  node->connection.store(*conn, std::memory_order_release);

  neighbours_.emplace(conn);
}

template <typename Vec>
static ConnectionPtr* findInVec(const Connection::Id& id, Vec& vec) {
  for (auto it = vec.begin(); it != vec.end(); ++it)
    if ((*it)->id == id)
      return it;

  return nullptr;
}

void Neighbourhood::connectNode(RemoteNodePtr node, ConnectionPtr conn) {
  conn->node = node;

  Connection* connection = nullptr;
  while (!node->connection.compare_exchange_strong(connection,
                                                   *conn,
                                                   std::memory_order_release,
                                                   std::memory_order_relaxed));

  if (connection) {
    LOG_WARN("Reconnected from " << connection->in << " to " << conn->in);
    auto connPtr = findInVec(connection->id, neighbours_);
    if (connPtr) neighbours_.remove(connPtr);
  }

  LOG_WARN("Connected to " << *conn);
  neighbours_.emplace(conn);
}

void Neighbourhood::gotRegistration(Connection&& conn,
                                    RemoteNodePtr node) {
  // Have we met?
  {
    SpinLock l(nLockFlag_);
    if (auto nh = findInVec(conn.id, neighbours_)) {
      auto oldConn = node->connection.load(std::memory_order_relaxed);
      if (oldConn && oldConn != **nh) {
        LOG_WARN("RemoteNode has a different connection" << oldConn->in << " vs " << (**nh)->in);
        if (oldConn->id > conn.id)
          return transport_->sendRegistrationRefusal(conn, RegistrationRefuseReasons::BadId);

        oldConn->id = conn.id;
      }
      return transport_->sendRegistrationConfirmation(conn);
    }
  }

  {
    SpinLock l(pLockFlag_);
    if (auto pc = findInVec(conn.id, pendingConnections_))
        pendingConnections_.remove(pc);
  }

  {
    SpinLock l(nLockFlag_);
    if (neighbours_.size() == MaxConnections)
      return transport_->sendRegistrationRefusal(conn, RegistrationRefuseReasons::LimitReached);

    ConnectionPtr newConn = connectionsAllocator_.emplace(std::move(conn));
    connectNode(node, newConn);
    transport_->sendRegistrationConfirmation(**newConn);
  }
}

void Neighbourhood::gotConfirmation(const Connection::Id& id, const ip::udp::endpoint& ep, const PublicKey& pk, RemoteNodePtr node) {
  ConnectionPtr tsPtr;

  {
    SpinLock l(pLockFlag_);
    ConnectionPtr* pc = findInVec(id, pendingConnections_);
    if (!pc) {
      LOG_WARN("Got confirmation with an unexpected ID");
      return;
    }

    tsPtr = *pc;
    pendingConnections_.remove(pc);
  }

  LOG_EVENT("Connection to " << ep << " established");

  if (ep != tsPtr->in) {
    tsPtr->out = tsPtr->in;
    tsPtr->specialOut = true;
    tsPtr->in = ep;
  }


  SpinLock l(nLockFlag_);
  connectNode(node, tsPtr);
}

void Neighbourhood::gotRefusal(const Connection::Id& id) {
  SpinLock l(pLockFlag_);
  ConnectionPtr* pc = findInVec(id, pendingConnections_);
  if (pc)
    pendingConnections_.remove(pc);
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

void Neighbourhood::validateConnectionId(RemoteNodePtr node,
                                         const Connection::Id id,
                                         const ip::udp::endpoint& ep) {
  SpinLock l(nLockFlag_);
  auto connection = node->connection.load(std::memory_order_acquire);
  if (!connection) {
    auto realPtr = findInVec(id, neighbours_);
    if (!realPtr) return;

    if ((*realPtr)->node.get() != node.get()) {
      (*realPtr)->node->connection.store(nullptr, std::memory_order_release);
      (*realPtr)->node = node;
    }

    (*realPtr)->in = ep;
    node->connection.store(realPtr->get(), std::memory_order_acquire);
  }
}
