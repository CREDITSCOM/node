/* Send blaming letters to @yrtimd */
#include <csnode/packstream.hpp>
#include <lib/system/allocators.hpp>
#include <lib/system/keys.hpp>

#include "network.hpp"
#include "transport.hpp"

enum RegFlags: uint8_t {
  UsingIPv6    = 1,
  RedirectPort = 1 << 1,
  RedirectIP   = 1 << 2
};

uint32_t CONNECTION_MAX_ATTEMPTS = 32;

namespace {
// Packets formation

void addMyOut(const Config& config, OPackStream& stream) {
  uint8_t regFlag = 0;
  if (!config.isSymmetric()) {
    if (config.getAddressEndpoint().ipSpecified) {
      regFlag|= RegFlags::RedirectIP;
      if (config.getAddressEndpoint().ip.is_v6())
        regFlag|= RegFlags::UsingIPv6;
    }

    regFlag|= RegFlags::RedirectPort;
  }
  else if (config.hasTwoSockets())
    regFlag|= RegFlags::RedirectPort;

  if (!config.isSymmetric()) {
    if (config.getAddressEndpoint().ipSpecified) {
      uint8_t* flagChar = stream.getCurrPtr();
      stream << config.getAddressEndpoint().ip;
      *flagChar|= regFlag;
    }
    else {
      stream << regFlag;
    }

    stream << config.getAddressEndpoint().port;
  }
  else if (config.hasTwoSockets()) {
    stream << regFlag << config.getInputEndpoint().port;
  }
  else
    stream << regFlag;
}

template <typename T>
T getSecureRandom() {
  T result;

  srand(time(NULL));            // ToFix: use libsodium here
  uint8_t* ptr = (uint8_t*)&result;
  for (uint32_t i = 0; i < sizeof(T); ++i, ++ptr)
    *ptr = rand() % 256;

  return result;
}

void formRegPack(const Config& config, OPackStream& stream, uint64_t** regPackConnId, const PublicKey& pk) {
  stream.init(BaseFlags::NetworkMsg);

  stream <<
    NetworkCommand::Registration <<
    NODE_VERSION;

  addMyOut(config, stream);
  *regPackConnId = (uint64_t*)stream.getCurrPtr();

  stream <<
    (ConnectionId)0 <<
    pk;
}

void formSSConnectPack(const Config& config, OPackStream& stream, const PublicKey& pk) {
  stream.init(BaseFlags::NetworkMsg);
  stream << NetworkCommand::SSRegistration
         << NODE_VERSION
         << (uint8_t)(config.getNodeType() == NodeType::Router);

  addMyOut(config, stream);

  stream << pk;
}
}

void Transport::run(const Config& config) {
  acceptRegistrations_ = config.getNodeType() == NodeType::Router;

  formRegPack(config, oPackStream_, &regPackConnId_, myPublicKey_);
  regPack_ = *(oPackStream_.getPackets());
  oPackStream_.clear();

  ConnectionId randomId = 0;
  if (config.getBootstrapType() == BootstrapType::IpList) {
    for (auto& ep : config.getIpList()) {
      auto elt = nh_.allocate();
      elt->endpoints.in = net_->resolve(ep);
      elt->connId = getSecureRandom<ConnectionId>();
      *(connectionsEnd_++) = elt;

      Packet req(netPacksAllocator_.allocateNext(regPack_.size()));
      *regPackConnId_ = elt->connId;
      memcpy(req.data(), regPack_.data(), regPack_.size());
    }
  }
  else {
    // Connect to SS logic
    ssEp_.specialOut = false;
    ssEp_.in = net_->resolve(config.getSignalServerEndpoint());

    LOG_EVENT("Connecting to Singal Server on " << ssEp_.in);

    formSSConnectPack(config, oPackStream_, myPublicKey_);
    ssStatus_ = SSBootstrapStatus::Requested;

    sendDirect(oPackStream_.getPackets(), ssEp_);
  }

  RegionAllocator allocator(100000l, 10);
  uint32_t ctr = 0;
  for (;;) {
    ++ctr;

    if (ctr % 30 == 0) {
      bool unestablished = false;
      for (auto connPtr = connections_; connPtr != connectionsEnd_; ++connPtr) {
        if (!(*connPtr)->placed.load(std::memory_order_relaxed)) {
          unestablished = true;
          LOG_EVENT("Sending connection request to " << (*connPtr)->endpoints.in);

          Packet req(netPacksAllocator_.allocateNext(regPack_.size()));
          *regPackConnId_ = (*connPtr)->connId;
          memcpy(req.data(), regPack_.data(), regPack_.size());

          sendDirect(&req, (*connPtr)->endpoints);
        }
      }

      if (!unestablished || connectAttempts_.load(std::memory_order_relaxed) == CONNECTION_MAX_ATTEMPTS)
        connectionsEnd_ = connections_;
      else
        connectAttempts_.fetch_add(1, std::memory_order_relaxed);
    }

    if (ctr % 3 == 0) {
      for (auto& c : sendPacks_) {
        sendBroadcast(&c);
        //LOG_WARN("RS " << byteStreamToHex((const char*)c.data(), 100));
      }
    }

    if (ctr % 30 == 0) {
      for (auto& nb : getNeighbourhood()) {
        //std::cout << nb.endpoints.in << ": " << nb.packetsCount.load(std::memory_order_relaxed) << std::endl;
      }

      Packet p(allocator.allocateNext(2));
      *static_cast<uint8_t*>(p.data()) = 1;
      *(static_cast<uint8_t*>(p.data()) + 1) = 5;
      sendBroadcast(&p);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}

template <>
inline uint16_t getHashIndex(const ip::udp::endpoint& ep) {
  uint16_t result = ep.port();

  if (ep.protocol() == ip::udp::v4()) {
    uint32_t addr = ep.address().to_v4().to_uint();
    result^= *(uint16_t*)&addr;
    result^= *((uint16_t*)&addr + 1);
  }
  else {
    auto bytes = ep.address().to_v6().to_bytes();
    auto ptr = (uint8_t*)&result;
    auto bytesPtr = bytes.data();
    for (uint32_t i = 0; i < 8; ++i) *ptr^= *(bytesPtr++);
    ++ptr;
    for (uint32_t i = 8; i < 16; ++i) *ptr^= *(bytesPtr++);
  }

  return result;
}

RemoteNode& Transport::getPackSenderEntry(const ip::udp::endpoint& ep) {
  RemoteNode& rn = remoteNodes_.tryStore(ep);

  if (rn.status == RemoteNode::Status::IsNeighbour)
    rn.neighbour->packetsCount.fetch_add(1, std::memory_order_relaxed);

  //LOG_WARN("SENDER is " << &rn);

  return rn;
}

// Processing network packages

void Transport::processNetworkTask(const TaskPtr<IPacMan>& task, RemoteNode& sender) {
  iPackStream_.init(task->pack.getMsgData(),
                    task->pack.getMsgSize());

  NetworkCommand cmd;
  iPackStream_ >> cmd;

  if (!iPackStream_.good())
    return sender.addStrike();

  switch (cmd) {
  case NetworkCommand::Registration:
    {
      LOG_EVENT("Got registration request from " << task->sender);

      if (/*sender.status != RemoteNode::Status::New  ||*/
          !acceptRegistrations_ ||
          !iPackStream_.canPeek<NodeVersion>()) {
        return sender.addStrike();
      }

      if (iPackStream_.peek<NodeVersion>() != NODE_VERSION) {
        sender.addStrike();
        return refuseRegistration(sender, RegistrationRefuseReasons::BadClientVersion);
      }

      iPackStream_.skip<NodeVersion>();
      if (!iPackStream_.canPeek<uint8_t>())
        return sender.addStrike();

      auto& flags = iPackStream_.peek<uint8_t>();

      if (!sender.neighbour)
        sender.neighbour = nh_.allocate();

      auto& eps = sender.neighbour->endpoints;
      eps.in = task->sender;

      eps.specialOut = false;
      if (flags & RegFlags::RedirectIP) {
        boost::asio::ip::address addr;
        iPackStream_ >> addr;

        eps.out.address(addr);
        eps.specialOut = true;
      }
      else {
        iPackStream_.skip<uint8_t>();
      }

      if (flags & RegFlags::RedirectPort) {
        Port port;
        iPackStream_ >> port;

        if (!eps.specialOut) {
          eps.specialOut = true;
          eps.out.address(task->sender.address());
        }

        eps.out.port(port);
      }
      else if (eps.specialOut)
        eps.out.port(task->sender.port());

      iPackStream_ >> sender.neighbour->connId;
      iPackStream_ >> sender.neighbour->key;

      if (!iPackStream_.good() || !iPackStream_.end()) {
        nh_.deallocate(sender.neighbour);

        return sender.addStrike();
      }

      return confirmRegistration(sender);
    }
  case NetworkCommand::ConfirmationRequest: break;
  case NetworkCommand::ConfirmationResponse: break;
  case NetworkCommand::RegistrationConfirmed:
    {
      LOG_EVENT("Confirming...");
      ConnectionId cId;
      iPackStream_ >> cId;
      if (!iPackStream_.good() ||
          !iPackStream_.canPeek<PublicKey>()) return sender.addStrike();

      bool found = false;
      for (auto ptr = connections_; ptr != connectionsEnd_; ++ptr) {
        if ((*ptr)->connId == cId) {
          sender.neighbour = *ptr;
          sender.status = RemoteNode::Status::IsNeighbour;

          if (task->sender != (*ptr)->endpoints.in) {
            (*ptr)->endpoints.out = (*ptr)->endpoints.in;
            (*ptr)->endpoints.specialOut = true;
            (*ptr)->endpoints.in = task->sender;
          }
          else
            (*ptr)->endpoints.specialOut = false;

          LOG_EVENT("Connection to " << task->sender << " established");
          nh_.insert(sender.neighbour);

          found = true;
          break;
        }
      }

      if (!found) {
        LOG_ERROR("Unknown registration");
        return sender.addStrike();
      }

      iPackStream_ >> sender.neighbour->key;
      return;
    }
  case NetworkCommand::RegistrationRefused:
    RegistrationRefuseReasons reason;
    iPackStream_ >> reason;
    if (!iPackStream_.good() || !iPackStream_.end())
      return sender.addStrike();

    LOG_EVENT("Registration to " << task->sender << " refused. Reason: " << (int)reason);
    break;
  case NetworkCommand::Ping:
    LOG_EVENT("Ping from " << task->sender);
    break;
  case NetworkCommand::SSRegistration:
    if (ssStatus_ != SSBootstrapStatus::Requested) {
      LOG_WARN("Unexpected Signal Server response");
      return;
    }

    LOG_EVENT("Connection to the Signal Server has been established");
    if (task->pack.getMsgSize() > 2) {
      if (!parseSSSignal(task))
        LOG_WARN("Bad Signal Server response");
    }
    else
      ssStatus_ = SSBootstrapStatus::RegisteredWait;

    break;
  case NetworkCommand::SSFirstRound:
    if (ssStatus_ != SSBootstrapStatus::RegisteredWait) {
      LOG_WARN("Unexpected Signal Server response");

      if (!parseSSSignal(task))
        LOG_WARN("Bad Signal Server response");

      return;
    }

    node_->getRoundTable(task->pack.getMsgData() + 2, task->pack.getMsgSize() - 2);
    ssStatus_ = SSBootstrapStatus::Complete;
    break;
  case NetworkCommand::SSRegistrationRefused:
	  uint16_t expectedVersion;
	  iPackStream_ >> expectedVersion;

	  LOG_ERROR("The Signal Server has refused the registration due to your bad client version. The expected version is " << expectedVersion);
	  break;
  default:
    sender.addStrike();
  }
}

bool Transport::parseSSSignal(const TaskPtr<IPacMan>& task) {
  iPackStream_.init(task->pack.getMsgData(), task->pack.getMsgSize());
  iPackStream_.safeSkip<uint8_t>(2);

  node_->getRoundTable(iPackStream_.getCurrPtr(), task->pack.getMsgSize() - 2);
  iPackStream_.safeSkip<uint32_t>();

  uint8_t numConf;
  iPackStream_ >> numConf;
  if (!iPackStream_.good()) return false;

  iPackStream_.safeSkip<PublicKey>(numConf + 1);

  uint8_t numCirc;
  iPackStream_ >> numCirc;
  if (!iPackStream_.good()) return false;

  for (uint8_t i = 0; i < numCirc; ++i) {
    iPackStream_.safeSkip<uint8_t>(2);
    ip::address ip;
    Port port;
    iPackStream_ >> ip >> port;
    if (!iPackStream_.good()) return false;

    auto elt = nh_.allocate();
    elt->endpoints.in = ip::udp::endpoint(ip, port);
    elt->connId = getSecureRandom<ConnectionId>();
    *(connectionsEnd_++) = elt;
  }

  connectAttempts_.store(0, std::memory_order_relaxed);
  ssStatus_ = SSBootstrapStatus::Complete;

  return true;
}

void Transport::processNodeMessage(const Message& msg) {
  dispatchNodeMessage(msg.getFirstPack(),
                      msg.getFullData(),
                      msg.getFullSize());
}

void Transport::processNodeMessage(const Packet& pack) {
  dispatchNodeMessage(pack, pack.getMsgData(), pack.getMsgSize());
}

void Transport::dispatchNodeMessage(const Packet& firstPack,
                                    const uint8_t* data,
                                    size_t size) {
  if (!size) {
    LOG_ERROR("Bad packet size, why is it zero?");
    return;
  }

  ++data;
  --size;

  switch(firstPack.getType()) {
  case MsgTypes::RoundTable:
    return node_->getRoundTable(data, size);
  case MsgTypes::Transactions:
    return node_->getTransaction(data, size);
  case MsgTypes::FirstTransaction:
    return node_->getFirstTransaction(data, size);
  case MsgTypes::TransactionList:
    return node_->getTransactionsList(data, size);
  case MsgTypes::ConsVector:
    return node_->getVector(data, size, firstPack.getSender());
  case MsgTypes::ConsMatrix:
    return node_->getMatrix(data, size, firstPack.getSender());
  case MsgTypes::NewBlock:
    return node_->getBlock(data, size, firstPack.getSender());
  case MsgTypes::BlockHash:
    return node_->getHash(data, size, firstPack.getSender());
  default:
    LOG_ERROR("Unknown type");
    break;
  }
}

void Transport::addTask(Packet* pack, const uint32_t packNum) {
  auto end = pack + packNum;
  for (auto ptr = pack; ptr != end; ++ptr) {
    sendBroadcast(ptr);
    {
      SpinLock l(sendPacksFlag_);
      sendPacks_.emplace(*ptr);
    }
  }
}

void Transport::clearTasks() {
  SpinLock l(sendPacksFlag_);
  sendPacks_.clear();
}

void Transport::refuseRegistration(RemoteNode& node, const RegistrationRefuseReasons reason) {
  LOG_EVENT("Refusing registration");

  oPackStream_.init(BaseFlags::NetworkMsg);
  oPackStream_ << NetworkCommand::RegistrationRefused << reason;

  sendDirect(oPackStream_.getPackets(), node.neighbour->endpoints);
  oPackStream_.clear();
}

void Transport::confirmRegistration(RemoteNode& node) {
  LOG_EVENT("Confirming registration with " << node.neighbour->endpoints.in << " on " << (node.neighbour->endpoints.specialOut ? node.neighbour->endpoints.out : node.neighbour->endpoints.in));

  node.status = RemoteNode::Status::IsNeighbour;

  oPackStream_.init(BaseFlags::NetworkMsg);
  oPackStream_ << NetworkCommand::RegistrationConfirmed << node.neighbour->connId<< myPublicKey_;

  sendDirect(oPackStream_.getPackets(), node.neighbour->endpoints);
  oPackStream_.clear();

  nh_.insert(node.neighbour);
}

void Transport::sendDirect(const Packet* pack, const NeighbourEndpoints& eps) {
  net_->sendDirect(*pack,
                   eps.specialOut ?
                   eps.out : eps.in);
}

void Transport::sendBroadcast(const Packet* pack) {
  //int i = 0;
  for (auto& nb : nh_) {
    //LOG_EVENT("FFF " << ++i);
    sendDirect(pack, nb.endpoints);
  }
}

Neighbourhood::Element* Neighbourhood::allocate() {
  SpinLock l(allocFlag_);
  return allocator_.emplace();
}

void Neighbourhood::deallocate(Element* elt) {
  SpinLock l(allocFlag_);
  return allocator_.remove(elt);
}

void Neighbourhood::insert(Element* elt) {
  LOG_WARN("Try Eltplus " << elt);
  bool tmp = false;
  if (!elt->placed.
      compare_exchange_strong(tmp,
                              true,
                              std::memory_order_acquire,
                              std::memory_order_relaxed))
    return;

  LOG_WARN("Eltplus " << elt);

  Element* firstPtr = first_.load(std::memory_order_acquire);
  do {
    elt->next.store(firstPtr, std::memory_order_relaxed);
  }
  while (!first_.compare_exchange_strong(firstPtr,
                                         elt,
                                         std::memory_order_release,
                                         std::memory_order_relaxed));
}
