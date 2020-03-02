//
// Created by User on 03.10.2018.
//

#ifndef PROJECT_TRANSPORTMOCK_HPP
#define PROJECT_TRANSPORTMOCK_HPP

#include <gmock/gmock.h>
#include "transport.hpp"

class TransportMock : public Transport {
public:
  MOCK_METHOD0(run, void());

  MOCK_METHOD1(processNodeMessage, void(const Packet&));

  MOCK_CONST_METHOD0(getMyPublicKey, const cs::PublicKey&());
  MOCK_CONST_METHOD0(isGood, bool());

  MOCK_METHOD1(processPostponed, void(const cs::RoundNumber));

  // private methods

  MOCK_METHOD3(postponePacket, void(const cs::RoundNumber, const MsgTypes, const Packet&));

  MOCK_METHOD5(dispatchNodeMessage,
               void(const MsgTypes, const cs::RoundNumber, const Packet&, const uint8_t* data, size_t));

  /* Network packages processing */
  MOCK_METHOD0(gotPing, bool());
};

#endif  // PROJECT_TRANSPORTMOCK_HPP
