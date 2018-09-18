/* Send blaming letters to @yrtimd */
#include <sodium.h>

#include "neighbourhood.hpp"
#include "transport.hpp"

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
  {
  SpinLock l(pLockFlag_);
  // If the connection cannot be established, retry it
  for (auto conn = pendingConnections_.begin();
       conn != pendingConnections_.end();
       ++conn) 
       {
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

 //LOG_EVENT("PENDING: ");
  //for (auto conn = pendingConnections_.begin();
  //     conn != pendingConnections_.end();
  //     ++conn)
  //  LOG_EVENT("- " << ((*conn)->specialOut ? "1" : "0") << " " << (*conn)->id << ", " << (*conn)->in << ", " << ((*conn)->specialOut ? (*conn)->out : (*conn)->in));
  }

  SpinLock ll(nLockFlag_);
  //LOG_EVENT("NEIGH: ");
  //for (auto conn = neighbours_.begin();
   //    conn != neighbours_.end();
   //    ++conn)
   // LOG_EVENT("- " << ((*conn)->specialOut ? "1" : "0") << " " << (*conn)->id << ", " << (*conn)->in << ", " << ((*conn)->specialOut ? (*conn)->out : (*conn)->in));
}

void Neighbourhood::checkSilent() {
  std::cout << __func__ << std::endl;
  bool needRefill = false;
  {
    SpinLock l(nLockFlag_);
    for (auto conn = neighbours_.begin();
         conn != neighbours_.end();
         ++conn) {
      const auto packetsCount = (*(*conn)->node)->packets.
        load(std::memory_order_relaxed);

      if (packetsCount == (*conn)->lastPacketsCount) {
       // LOG_WARN("Node " << (*conn)->in << " stopped responding");
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

    SpinLock ll(pLockFlag_);
    needRefill = ((neighbours_.size() + pendingConnections_.size()) < MinConnections);
  }

  if (needRefill) transport_->refillNeighbourhood();
}

void Neighbourhood::establishConnection(const ip::udp::endpoint& ep) {
  SpinLock ll(pLockFlag_);

  ConnectionPtr& conn =
    pendingConnections_.emplace(connectionsAllocator_.emplace());

  conn->id = getSecureRandom<Connection::Id>();
  conn->in = ep;

  transport_->sendRegistrationRequest(**conn);
}

void Neighbourhood::addSignalServer(const ip::udp::endpoint& in, const ip::udp::endpoint& out, RemoteNodePtr node) {
  if ((*node)->connection.load(std::memory_order_relaxed)) return;

  ConnectionPtr conn = connectionsAllocator_.emplace();

  conn->id = getSecureRandom<Connection::Id>();
  conn->in = in;
  if (in != out) {
    conn->specialOut = true;
    conn->out = out;
  }

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

  neighbours_.emplace(conn);
}

void Neighbourhood::gotRegistration(Connection&& conn,
                                    RemoteNodePtr node) {
  // Have we met?
  {
    SpinLock l(nLockFlag_);
    if (auto nh = findInVec(conn.id, neighbours_)) {
      if (node->connection.load(std::memory_order_relaxed) != **nh) {
        LOG_WARN("RemoteNode has a different connection");
        return transport_->sendRegistrationRefusal(conn, RegistrationRefuseReasons::BadId);
      }
      return transport_->sendRegistrationConfirmation(conn);
    }
  }

  {
    SpinLock l(pLockFlag_);
    if (auto pc = findInVec(conn.id, pendingConnections_)) {
      //if ((conn.specialOut && conn.out == (*pc)->in) ||
      //(!conn.specialOut && conn.in == (*pc)->in))
        pendingConnections_.remove(pc);
        //else {
        //LOG_WARN("Bad connection of id " << conn.id << ": " << (conn.specialOut ? "1" : "0") << " " << (conn.specialOut ? conn.out : conn.in) << " " << (*pc)->in);
        //return transport_->sendRegistrationRefusal(conn, RegistrationRefuseReasons::BadId);
        //}
    }
  }

  {
    SpinLock l(nLockFlag_);
    if (neighbours_.size() == MaxConnections)
      return transport_->sendRegistrationRefusal(conn, RegistrationRefuseReasons::LimitReached);

    ConnectionPtr newConn = connectionsAllocator_.emplace(std::move(conn));
    connectNode(node, newConn);
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


  SpinLock l(nLockFlag_);
  connectNode(node, tsPtr);
}

void Neighbourhood::gotRefusal(const Connection::Id& id) {
  SpinLock l(pLockFlag_);
  ConnectionPtr* pc = findInVec(id, pendingConnections_);
  if (pc)
    pendingConnections_.remove(pc);
}
