//
// Created by User on 04.10.2018.
//

#ifndef PROJECT_NETWORKMOCK_HPP
#define PROJECT_NETWORKMOCK_HPP

#include <gmock/gmock.h>
#include "network.hpp"

class NetworkMock : public Network {
public:
  MOCK_CONST_METHOD0(isGood, bool());
  MOCK_METHOD1(resolve, ip::udp::endpoint(const EndpointData&));

  MOCK_METHOD2(sendDirect, void(const Packet, const ip::udp::endpoint&));

  MOCK_METHOD3(resendFragment, bool(const cs::Hash&, const uint16_t, const ip::udp::endpoint&));
  MOCK_METHOD2(registerMessage, void(Packet*, const uint32_t size));

  // private methods

  MOCK_METHOD1(readerRoutine, void(const Config&));
  MOCK_METHOD1(writerRoutine, void(const Config&));
  MOCK_METHOD0(processorRoutine, void());

  MOCK_METHOD4(getSocketInThread, ip::udp::socket*(const bool, const EndpointData&, std::atomic<ThreadStatus>&, const bool useIPv6));
};

#endif //PROJECT_NETWORKMOCK_HPP
