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

  MOCK_METHOD1(getPackSenderEntry, RemoteNodePtr(const ip::udp::endpoint&));

  MOCK_METHOD1(processNodeMessage, void(const Packet&));

  MOCK_METHOD4(addTask, void(Packet*, const uint32_t packNum, bool incrementWhenResend, bool sendToNeighbours));
  MOCK_METHOD0(clearTasks, void());

  MOCK_CONST_METHOD0(getMyPublicKey, const cs::PublicKey&());
  MOCK_CONST_METHOD0(isGood, bool());

  MOCK_METHOD1(sendBroadcast, void(const Packet* pack));

  MOCK_METHOD2(sendDirect, void(const Packet* pack, const Connection& conn));

  MOCK_METHOD2(gotPacket, void(const Packet&, RemoteNodePtr&));
  MOCK_METHOD1(redirectPacket, void(const Packet&));

  MOCK_METHOD0(refillNeighbourhood, void());
  MOCK_METHOD1(processPostponed, void(const cs::RoundNumber));

  MOCK_METHOD0(sendRegistrationRequest, void());
  MOCK_METHOD0(sendRegistrationConfirmation, void());
  MOCK_METHOD1(sendRegistrationRefusal, void(const RegistrationRefuseReasons));

  MOCK_METHOD0(sendPingPack, void());

  // private methods

  MOCK_METHOD3(postponePacket, void(const cs::RoundNumber, const MsgTypes, const Packet&));

  MOCK_METHOD5(dispatchNodeMessage,
               void(const MsgTypes, const cs::RoundNumber, const Packet&, const uint8_t* data, size_t));

  /* Network packages processing */
  MOCK_METHOD0(gotRegistrationRequest, bool());
  MOCK_METHOD0(gotRegistrationConfirmation, bool());
  MOCK_METHOD0(gotRegistrationRefusal, bool());

  MOCK_METHOD0(gotPing, bool());

  MOCK_METHOD0(askForMissingPackages, void());
  MOCK_METHOD3(requestMissing, void(const cs::Hash&, const uint16_t, const uint64_t));
};

#endif  // PROJECT_TRANSPORTMOCK_HPP
