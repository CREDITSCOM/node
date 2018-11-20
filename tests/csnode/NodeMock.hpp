//
// Created by User on 04.10.2018.
//

#ifndef PROJECT_NODEMOCK_HPP
#define PROJECT_NODEMOCK_HPP

#include <gmock/gmock.h>
#include "csnode/nodecore.hpp"
#include "csnode/node.hpp"

class NodeMock : public Node
{
public:
  MOCK_CONST_METHOD0(isGood, bool());
  MOCK_METHOD1(run, void(const Config&));

  /* Incoming requests processing */
  MOCK_METHOD4(getRoundTable, void(const uint8_t*, const size_t, const cs::RoundNumber, uint8_t type));
  MOCK_METHOD2(getTransaction, void(const uint8_t*, const size_t));
  MOCK_METHOD2(getFirstTransaction, void(const uint8_t*, const size_t));
  MOCK_METHOD2(getTransactionsList, void(const uint8_t*, const size_t));
  MOCK_METHOD3(getVector, void(const uint8_t*, const size_t, const cs::PublicKey& sender));
  MOCK_METHOD3(getMatrix, void(const uint8_t*, const size_t, const cs::PublicKey& sender));
  MOCK_METHOD3(getBlock, void(const uint8_t*, const size_t, const cs::PublicKey& sender));
  MOCK_METHOD3(getHash, void(const uint8_t*, const size_t, const cs::PublicKey& sender));
  MOCK_METHOD2(getTransactionsPacket, void(const uint8_t*, const std::size_t));

  // transaction's pack syncro
  MOCK_METHOD3(getPacketHashesRequest, void(const uint8_t*, const std::size_t, const cs::PublicKey& sender));
  MOCK_METHOD2(getPacketHashesReply, void(const uint8_t*, const std::size_t));

  MOCK_METHOD3(getRoundTableUpdated, void(const uint8_t*, const size_t, const cs::RoundNumber));
  MOCK_METHOD3(getCharacteristic, void(const uint8_t* data, const size_t size, const cs::PublicKey& sender));

  MOCK_METHOD2(getWriterNotification, void(const uint8_t* data, const std::size_t size));
  MOCK_METHOD0(sendNotificationToWriter, void());

  /*syncro get functions*/
  MOCK_METHOD3(getBlockRequest, void(const uint8_t*, const size_t, const cs::PublicKey& sender));
  MOCK_METHOD2(getBlockReply, void(const uint8_t*, const size_t));
  MOCK_METHOD3(getWritingConfirmation, void(const uint8_t* data, const size_t size, const cs::PublicKey& sender));

  /* Outcoming requests forming */
  MOCK_METHOD0(sendRoundTable, void());
  MOCK_METHOD1(sendTransaction, void(const csdb::Transaction&));

  MOCK_METHOD1(sendFirstTransaction, void(const csdb::Transaction&));
  MOCK_METHOD1(sendTransactionList, void(const csdb::Pool&));
  MOCK_METHOD1(sendVector, void(const cs::HashVector&));
  MOCK_METHOD1(sendMatrix, void(const cs::HashMatrix&));
  MOCK_METHOD1(sendBlock, void(const csdb::Pool&));
  MOCK_METHOD2(sendHash, void(const std::string&, const cs::PublicKey&));

  // transaction's pack syncro
  MOCK_METHOD1(sendTransactionsPacket, void(const cs::TransactionsPacket& packet));
  MOCK_METHOD2(sendPacketHashesRequest, void(const cs::Hashes& hashes, const cs::RoundNumber round));
  MOCK_METHOD2(sendPacketHashesRequestToRandomNeighbour, void(const cs::Hashes& hashes, const cs::RoundNumber round));
  MOCK_METHOD3(sendPacketHashesReply, void(const cs::Packets& packet, const cs::RoundNumber round, const cs::PublicKey& sender));

  MOCK_METHOD3(sendCharacteristic, void(const csdb::Pool& emptyMetaPool, const uint32_t maskBitsCount, const std::vector<uint8_t>& characteristic));

  /*syncro send functions*/
  MOCK_METHOD1(sendBlockRequest, void(uint32_t seq));
  MOCK_METHOD2(sendBlockReply, void(const csdb::Pool&, const cs::PublicKey&));

  MOCK_METHOD0(flushCurrentTasks, void());
  MOCK_METHOD0(becomeWriter, void());
  MOCK_METHOD1(initNextRound, void(const cs::RoundTable& roundInfo));
  MOCK_METHOD0(isPoolsSyncroStarted, bool());

  MOCK_METHOD2(chooseMessageActionm, MessageActions(const cs::RoundNumber, const MsgTypes));

  MOCK_CONST_METHOD0(getMyPublicKey, const cs::PublicKey&());
  MOCK_CONST_METHOD0(getMyLevel, NodeLevel());
  MOCK_METHOD0(getRoundNumber, uint32_t());
  MOCK_METHOD0(getMyConfNumber, uint8_t());

  MOCK_CONST_METHOD0(getConfidants, const std::vector<cs::PublicKey>&());

  MOCK_METHOD0(getBlockChain, BlockChain&());
  MOCK_CONST_METHOD0(getBlockChain, const BlockChain&());

#ifdef NODE_API
  MOCK_METHOD0(getConnector, csconnector::connector&());
#endif

  MOCK_METHOD1(addToPackageTemporaryStorage, void(const csdb::Pool& pool));

  // private methods

  MOCK_METHOD0(init, bool());

  // signature verification
  MOCK_METHOD0(checkKeysFile, bool());
  MOCK_METHOD0(generateKeys, void());
  MOCK_METHOD0(checkKeysForSignature, bool());

  MOCK_METHOD1(readRoundData, bool(bool));
  MOCK_METHOD0(onRoundStart, void());

  MOCK_METHOD2(composeMessageWithBlock, void(const csdb::Pool&, const MsgTypes));
};

#endif //PROJECT_NODEMOCK_HPP
