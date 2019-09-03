//
// Created by User on 03.10.2018.
//

#ifndef PROJECT_SOLVERMOCK_HPP
#define PROJECT_SOLVERMOCK_HPP

#include <gmock/gmock.h>
#include <solver/solvercore.hpp>

class SolverMock : public cs::SolverCore {
public:
  MOCK_CONST_METHOD0(getMyPublicKey, cs::PublicKey());
  MOCK_CONST_METHOD0(getPathToDB, std::string());
  MOCK_METHOD2(set_keys, void(const std::vector<uint8_t>& pub, const std::vector<uint8_t>& priv));
  MOCK_METHOD1(gotTransactionsPacket, void(cs::TransactionsPacket&& packet));
  MOCK_METHOD2(gotPacketHashesRequest,
               void(std::vector<cs::TransactionsPacketHash>&& hashes, const cs::PublicKey& sender));
  MOCK_METHOD1(gotPacketHashesReply, void(cs::TransactionsPacket&& packet));
  MOCK_METHOD1(gotRound, void(cs::RoundTable&& round));
  MOCK_METHOD1(gotBlockCandidate, void(csdb::Pool&&));
  MOCK_METHOD2(gotHash, void(std::string&&, const cs::PublicKey&));
  MOCK_METHOD2(gotBlockRequest, void(csdb::PoolHash&&, const cs::PublicKey&));
  MOCK_METHOD1(gotBlockReply, void(csdb::Pool&&));
  MOCK_METHOD2(gotBadBlockHandler, void(csdb::Pool&&, const cs::PublicKey&));
  MOCK_METHOD4(applyCharacteristic, void(const std::vector<uint8_t>& characteristic, const uint32_t bitsCount,
                                         const csdb::Pool& metaInfoPool, const cs::PublicKey& sender));
  MOCK_CONST_METHOD0(getCharacteristicHash, cs::Hash());
  MOCK_METHOD0(getSignedNotification, std::vector<uint8_t>());

  MOCK_CONST_METHOD0(getWriterPublicKey, cs::PublicKey());

  MOCK_METHOD0(getSignature, const char*());

  // API methods
  MOCK_METHOD0(initApi, void());
  MOCK_METHOD0(addInitialBalance, void());

  MOCK_METHOD0(currentRoundNumber, cs::RoundNumber());
  MOCK_METHOD1(addTransaction, void(const csdb::Transaction& transaction));

  MOCK_METHOD1(send_wallet_transaction, void(const csdb::Transaction& transaction));

  MOCK_METHOD0(nextRound, void());
  MOCK_METHOD0(isPoolClosed, bool());
  MOCK_METHOD1(setLastRoundTransactionsGot, void(size_t trNum));

  // remove it!!!
  MOCK_METHOD1(buildBlock, void(csdb::Pool& block));
  MOCK_METHOD0(buildTransactionList, void());

  MOCK_METHOD0(initConfRound, void());
  MOCK_METHOD0(sendZeroVector, void());
  MOCK_METHOD1(checkVectorsReceived, void(size_t _rNum));
  MOCK_METHOD0(checkMatrixReceived, void());
  MOCK_METHOD1(addConfirmation, void(uint8_t confNumber_));
  MOCK_METHOD0(getIPoolClosed, bool());
  MOCK_METHOD0(getBigBangStatus, bool());
  MOCK_METHOD1(setBigBangStatus, void(bool _status));
  MOCK_METHOD1(setRNum, void(size_t _rNum));
  MOCK_METHOD3(setConfidants, void(const std::vector<cs::PublicKey>& confidants, const cs::PublicKey& general,
                                   const cs::RoundNumber roundNum));

  // private methods

  MOCK_METHOD0(_initApi, void());

  MOCK_METHOD0(runMainRound, void());
  MOCK_METHOD0(closeMainRound, void());

  MOCK_METHOD0(flushTransactions, void());

  MOCK_METHOD0(writeNewBlock, void());
  MOCK_METHOD1(prepareBlockForSend, void(csdb::Pool& block));

  MOCK_METHOD4(verify_signature,
               bool(uint8_t signature[64], uint8_t public_key[32], uint8_t* message, size_t message_len));
};

#endif  // PROJECT_SOLVERMOCK_HPP
