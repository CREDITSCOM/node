/* Send blaming letters to @yrtimd */
#include <lib/system/logger.hpp>

#include "network.hpp"
#include "transport.hpp"

const ip::udp::socket::message_flags NO_FLAGS = 0;

static ip::udp::socket bindSocket(io_context& context, Network* net, const EndpointData& data, bool ipv6 = true) {
  try {
    ip::udp::socket sock(context, ipv6 ? ip::udp::v6() : ip::udp::v4());

    if (ipv6) sock.set_option(ip::v6_only(false));

    sock.set_option(ip::udp::socket::reuse_address(true));
    sock.set_option(ip::udp::socket::send_buffer_size(Packet::MaxSize));

    if (data.ipSpecified) {
      auto ep = net->resolve(data);
      sock.bind(ep);
    }
    else
      sock.bind(ip::udp::endpoint(ipv6 ? ip::udp::v6() : ip::udp::v4(),
                                  data.port));

    return sock;
  }
  catch(boost::system::system_error& e) {
    LOG_ERROR("Cannot bind socket on " << e.what());
    return ip::udp::socket(context);
  }
}

ip::udp::endpoint Network::resolve(const EndpointData& data) {
  return ip::udp::endpoint(data.ip, data.port);
}

ip::udp::socket* Network::getSocketInThread(const bool openOwn,
                                            const EndpointData& epd,
                                            std::atomic<Network::ThreadStatus>& status,
                                            const bool ipv6) {
  ip::udp::socket* result = nullptr;

  if (openOwn) {
    result = new ip::udp::socket(bindSocket(context_, this, epd, ipv6));
    if (!result->is_open()) result = nullptr;
  }
  else {
    while (!singleSockOpened_.load());
    result = singleSock_.load();
  }

  status.store(result ? ThreadStatus::Success : ThreadStatus::Failed);

  return result;
}

void Network::readerRoutine(const Config& config) {
  ip::udp::socket* sock = getSocketInThread(config.hasTwoSockets(),
                                            config.getInputEndpoint(),
                                            readerStatus_,
                                            config.useIPv6());

  if (!sock) return;

  boost::system::error_code lastError;

  for (;;) {
    auto& task = iPacMan_.allocNext();
    task.size =
      sock->receive_from(buffer(task.pack.data(),
                                Packet::MaxSize),
                         task.sender,
                         NO_FLAGS,
                         lastError);

    if (!lastError) {
      iPacMan_.enQueueLast();
    }
    else
      LOG_ERROR("Cannot receive packet. Error " << lastError);
  }
}

static inline void sendPack(ip::udp::socket& sock, TaskPtr<OPacMan>& task, const ip::udp::endpoint& ep) {
  boost::system::error_code lastError;

  LOG_OUT_PACK(task->pack.data(), task->pack.size());

  auto size = sock.send_to(buffer(task->pack.data(), task->pack.size()),
                           ep,
                           NO_FLAGS,
                           lastError);

  if (lastError || size < task->pack.size())
    LOG_ERROR("Cannot send packet. Error " << lastError);
}

void Network::writerRoutine(const Config& config) {
  ip::udp::socket* sock = getSocketInThread(config.hasTwoSockets(),
                                            config.getOutputEndpoint(),
                                            writerStatus_,
                                            config.useIPv6());

  if (!sock) return;

  for (;;) {
    auto task = oPacMan_.getNextTask();
    sendPack(*sock, task, task->endpoint);
  }
}

// Processors

void Network::processorRoutine() {
  FixedHashMap<Hash, uint32_t, uint16_t> packetMap;
  PacketCollector collector;
  CallsQueue& externals = CallsQueue::instance();

  for (;;) {
    externals.callAll();

    auto task = iPacMan_.getNextTask();
    LOG_IN_PACK(task->pack.data(), task->pack.size());

    auto remoteSender = transport_->getPackSenderEntry(task->sender);
    if (remoteSender->isBlackListed()) {
      LOG_WARN("Blacklisted");
      continue;
    }

    if (!(task->pack.isHeaderValid())) {
      LOG_WARN("Header is not valid");
      remoteSender->addStrike();
      continue;
    }

    // Pure network processing
    if (task->pack.isNetwork()) {
      transport_->processNetworkTask(task, remoteSender);
      continue;
    }

    // Non-network data
    uint32_t& recCounter = packetMap.tryStore(task->pack.getHash());
    if (!recCounter && task->pack.addressedToMe(transport_->getMyPublicKey())) {
      if (task->pack.isFragmented() || task->pack.isCompressed()) {
        Message& msg = collector.getMessage(task->pack);
        if (msg.isComplete())
          transport_->processNodeMessage(msg);
      }
      else
        transport_->processNodeMessage(task->pack);
    }

    if (recCounter < OPacMan::MaxTimesRedirect)
      transport_->sendBroadcast(&task->pack);

    ++recCounter;
  }
}

void Network::sendDirect(const Packet p, const ip::udp::endpoint& ep) {
  auto qePtr = oPacMan_.allocNext();

  qePtr->element.endpoint = ep;
  qePtr->element.pack = p;

  oPacMan_.enQueueLast(qePtr);
}

Network::Network(const Config& config, Transport* transport):
  resolver_(context_),
  transport_(transport),
  readerThread_(&Network::readerRoutine, this, config),
  writerThread_(&Network::writerRoutine, this, config),
  processorThread_(&Network::processorRoutine, this) {

  if (!config.hasTwoSockets()) {
    auto sockPtr = new ip::udp::socket(bindSocket(context_,
                                                  this,
                                                  config.getInputEndpoint(),
                                                  config.useIPv6()));

    if (!sockPtr->is_open()) {
      good_ = false;
      return;
    }

    singleSock_.store(sockPtr);
    singleSockOpened_.store(true);
  }

  while (readerStatus_.load() == ThreadStatus::NonInit);
  while (writerStatus_.load() == ThreadStatus::NonInit);

  good_ = (readerStatus_.load() == ThreadStatus::Success &&
           writerStatus_.load() == ThreadStatus::Success);

  if (!good_)
    LOG_ERROR("Cannot start the network: error binding sockets");
}

Network::~Network() {
  readerThread_.join();
  writerThread_.join();
  processorThread_.join();
}
