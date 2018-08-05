#include "neighbourhood.hpp"
#include "transport.hpp"

template <typename T>
T getSecureRandom() {
  T result;

  srand(time(NULL));            // ToFix: use libsodium here
  uint8_t* ptr = (uint8_t*)&result;
  for (uint32_t i = 0; i < sizeof(T); ++i, ++ptr)
    *ptr = rand() % 256;

  return result;
}

void Neighbourhood::sendByNeighbours(const Packet* pack) {
  SpinLock l(nLockFlag_);
  for (auto& nb : neighbours_)
    transport_->sendDirect(pack, **nb);
}

void Neighbourhood::checkPending() {
  SpinLock l(pLockFlag_);
  // If the connection cannot be established, retry it
  for (auto conn = pendingConnections_.begin();
       conn != pendingConnections_.end();
       ++conn) {
    // Attempt to reconnect if the connection hasn't been established yet
    if ((*conn)->attempts < Neighbourhood::MaxConnectAttempts)
      transport_->sendRegistrationRequest(***conn);
    else {
      pendingConnections_.remove(conn);
      --conn;
    }
  }
}

void Neighbourhood::checkSilent() {
  SpinLock l(nLockFlag_);
  for (auto conn = neighbours_.begin();
           conn != neighbours_.end();
           ++conn) {
    const auto packetsCount = (*(*conn)->node)->packets.
      load(std::memory_order_relaxed);

    if (packetsCount == (*conn)->lastPacketsCount) {
      LOG_WARN("Node " << (*conn)->in << " stopped responding");
      ConnectionPtr tc = *conn;
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
}

void Neighbourhood::establishConnection(const ip::udp::endpoint& ep) {
  ConnectionPtr& conn =
    pendingConnections_.emplace(connectionsAllocator_.emplace());

  conn->id = getSecureRandom<Connection::Id>();
  conn->in = ep;

  transport_->sendRegistrationRequest(**conn);
}

template <typename Vec>
static ConnectionPtr* findInVec(const Connection::Id& id, Vec& vec) {
  for (auto it = vec.begin(); it != vec.end(); ++it)
    if ((*it)->id == id)
      return it;

  return nullptr;
}

void Neighbourhood::gotRegistration(Connection&& conn,
                                    RemoteNodePtr node) {
  // Have we met?
  {
    SpinLock l(nLockFlag_);
    if (auto nh = findInVec(conn.id, neighbours_)) {
      if (node->connection.load(std::memory_order_relaxed) != **nh)
        return transport_->sendRegistrationRefusal(conn, RegistrationRefuseReasons::BadId);
      return transport_->sendRegistrationConfirmation(conn);
    }
  }

  {
    SpinLock l(pLockFlag_);
    if (auto pc = findInVec(conn.id, pendingConnections_)) {
      if ((conn.specialOut && conn.out == (*pc)->in) ||
          (!conn.specialOut && conn.in == (*pc)->in))
        pendingConnections_.remove(pc);
      else {
        return transport_->sendRegistrationRefusal(conn, RegistrationRefuseReasons::BadId);
      }
    }
  }

  {
    SpinLock l(nLockFlag_);
    if (neighbours_.size() == MaxConnections)
      return transport_->sendRegistrationRefusal(conn, RegistrationRefuseReasons::LimitReached);

    ConnectionPtr newConn = connectionsAllocator_.emplace(std::move(conn));
    neighbours_.emplace(newConn);
    newConn->node = node;
    node->connection.store(*newConn, std::memory_order_relaxed);
  }

  transport_->sendRegistrationConfirmation(conn);
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
  else
    tsPtr->specialOut = false;


  SpinLock l(nLockFlag_);
  neighbours_.emplace(tsPtr);
  tsPtr->node = node;
  node->connection.store(*tsPtr,
                         std::memory_order_relaxed);
}

void Neighbourhood::gotRefusal(const Connection::Id& id) {
  SpinLock l(pLockFlag_);
  ConnectionPtr* pc = findInVec(id, pendingConnections_);
  if (pc)
    pendingConnections_.remove(pc);
}
