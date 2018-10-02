/* Send blaming letters to @yrtimd */
#ifndef __NETWORK_HPP__
#define __NETWORK_HPP__
#include <boost/asio.hpp>

#include <client/config.hpp>
#include <lib/system/cache.hpp>
#include "pacmans.hpp"

class Transport;
class Network {
public:
  Network(const Config&, Transport*);
  ~Network();

  bool isGood() const { return good_; }
  ip::udp::endpoint resolve(const EndpointData&);

  void sendDirect(const Packet&, const ip::udp::endpoint&);

  bool resendFragment(const Hash&, const uint16_t, const ip::udp::endpoint&);
  void registerMessage(Packet*, const uint32_t size);

  Network(const Network&) = delete;
  Network(Network&&) = delete;
  Network& operator=(const Network&) = delete;
  Network& operator=(Network&&) = delete;

private:
  void readerRoutine(const Config&);
  void writerRoutine(const Config&);
  void processorRoutine();

  enum ThreadStatus {
    NonInit,
    Failed,
    Success
  };
  ip::udp::socket* getSocketInThread(const bool,
                                     const EndpointData&,
                                     std::atomic<ThreadStatus>&,
                                     const bool useIPv6);

  bool good_;

  io_context context_;
  ip::udp::resolver resolver_;

  IPacMan iPacMan_;
  OPacMan oPacMan_;

  Transport* transport_;

  // Only needed in a one-socket configuration
  __cacheline_aligned std::atomic<bool> singleSockOpened_ = { false };
  __cacheline_aligned std::atomic<ip::udp::socket*> singleSock_ = { nullptr };

  __cacheline_aligned std::atomic<ThreadStatus> readerStatus_ = { NonInit };
  __cacheline_aligned std::atomic<ThreadStatus> writerStatus_ = { NonInit };

  std::thread readerThread_;
  std::thread writerThread_;
  std::thread processorThread_;

  PacketCollector collector_;
};

#endif // __NETWORK_HPP__
