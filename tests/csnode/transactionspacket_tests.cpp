#define TESTING

#include <gtest/gtest.h>
#include <algorithm>
#include <vector>

#include "transactionspacket.hpp"

#include <csdb/transaction.hpp>
#include <csdb/address.hpp>
#include <csdb/currency.hpp>
#include <cscrypto/cscrypto.hpp>

#include <lib/system/utils.hpp>

static cs::TransactionsPacket packet;

static csdb::Transaction makeTransaction(int64_t innerId) {
  csdb::Transaction transaction;

  auto startAddress = csdb::Address::from_string("0000000000000000000000000000000000000000000000000000000000000007");
  cs::PublicKey myPublicForSig;

  transaction.set_target(csdb::Address::from_public_key(myPublicForSig));
  transaction.set_source(startAddress);
  transaction.set_currency(1);
  transaction.set_amount(csdb::Amount(10000, 0));
  transaction.set_innerID(innerId);

  return transaction;
}

TEST(TransactionsPacket, createPacket) {
  ASSERT_TRUE(packet.isHashEmpty());
}

TEST(TransactionsPacket, addTransactions) {
  auto startAddress = csdb::Address::from_string("0000000000000000000000000000000000000000000000000000000000000007");
  cs::PublicKey myPublicForSig;

  csdb::Transaction transaction;
  transaction.set_target(csdb::Address::from_public_key(myPublicForSig));
  transaction.set_source(startAddress);
  transaction.set_currency(1);
  transaction.set_amount(csdb::Amount(10000, 0));

  const size_t randomTransactionsCount = cs::Utils::generateRandomValue<size_t>(3, 30);
  const size_t startInnerID = cs::Utils::generateRandomValue<size_t>(1, 2789);

  const auto oldtransactionsCount = packet.transactionsCount();

  for (std::size_t i = 0; i < randomTransactionsCount; ++i) {
    transaction.set_innerID(static_cast<int64_t>(i + startInnerID));
    ASSERT_TRUE(packet.addTransaction(transaction));
  }

  const auto& newtransactionsCount = packet.transactionsCount();
  ASSERT_NE(oldtransactionsCount, newtransactionsCount);
  ASSERT_EQ(newtransactionsCount, randomTransactionsCount);
}

TEST(TransactionsPacket, makeHash) {
  const auto oldMainHash = packet.hash();

  packet.makeHash();

  ASSERT_FALSE(packet.isHashEmpty());
  ASSERT_NE(oldMainHash, packet.hash());
}

TEST(TransactionsPacket, makeNewHash) {
  const auto oldMainHash = packet.hash();
  const auto oldtransactionsCount = packet.transactionsCount();

  packet.makeHash();

  ASSERT_EQ(oldMainHash, packet.hash());
  ASSERT_EQ(oldtransactionsCount, packet.transactionsCount());
}

TEST(TransactionsPacket, makeHashFromString) {
  cs::TransactionsPacketHash hashFromString;
  ASSERT_TRUE(hashFromString.isEmpty());
  const auto& mainHash = packet.hash();
  const std::string mainHashStr = mainHash.toString();
  hashFromString = cs::TransactionsPacketHash::fromString(mainHashStr);

  ASSERT_EQ(mainHash, hashFromString);
  ASSERT_EQ(mainHashStr, hashFromString.toString());
  ASSERT_EQ(mainHash.toBinary(), hashFromString.toBinary());
}

TEST(TransactionsPacket, makeHashFromBinary) {
  const auto& mainHash = packet.hash();
  const auto& mainHashBin = mainHash.toBinary();
  const cs::TransactionsPacketHash hashFromBinary = cs::TransactionsPacketHash::fromBinary(mainHashBin);
  ASSERT_EQ(mainHash, hashFromBinary);
  ASSERT_EQ(mainHashBin, hashFromBinary.toBinary());
  ASSERT_EQ(mainHash.toString(), hashFromBinary.toString());
}

TEST(TransactionsPacket, makeTransactionsPacketFromBinary) {
  const auto& mainPacketBin = packet.toBinary();
  const cs::TransactionsPacket packetFromBinary = cs::TransactionsPacket::fromBinary(mainPacketBin);

  ASSERT_EQ(mainPacketBin, packetFromBinary.toBinary());
  ASSERT_EQ(packet.transactionsCount(), packetFromBinary.transactionsCount());

  const auto& mainHash = packet.hash();
  const auto& packetFromBinaryHash = packetFromBinary.hash();

  ASSERT_EQ(mainHash, packetFromBinaryHash);
  ASSERT_EQ(mainHash.toBinary(), packetFromBinaryHash.toBinary());
}

TEST(TransactionsPacket, makeTransactionsPacketFromByteStream) {
  const auto mainPacketBin = packet.toBinary();
  const size_t rawSize = mainPacketBin.size();
  const void* rawData = mainPacketBin.data();

  const cs::TransactionsPacket hashFromStream = cs::TransactionsPacket::fromByteStream(static_cast<const char*>(rawData), rawSize);

  ASSERT_EQ(mainPacketBin, hashFromStream.toBinary());
  ASSERT_EQ(packet.toBinary(), hashFromStream.toBinary());
  ASSERT_EQ(packet.transactionsCount(), hashFromStream.transactionsCount());

  const auto& mainHash = packet.hash();
  const auto& hashFromStreamHash = hashFromStream.hash();

  ASSERT_EQ(mainHash, hashFromStreamHash);
  ASSERT_EQ(mainHash.toBinary(), hashFromStreamHash.toBinary());
}

TEST(TransactionsPacket, signaturesSerialization) {
  constexpr size_t maxTransactionsCount = 100;
  const size_t count = cs::Utils::generateRandomValue<size_t>(0, maxTransactionsCount);
  cs::Console::writeLine("Generated transactions count ", count);

  cs::TransactionsPacket pack;

  for (size_t i = 0; i < count; ++i) {
    pack.addTransaction(makeTransaction(static_cast<int64_t>(i)));
  }

  cs::Signature signature;

  // 0
  signature.fill(0xFF);
  pack.addSignature(0, signature);

  // 1
  signature.fill(0xAA);
  pack.addSignature(1, signature);

  // 2
  signature.fill(0xDD);
  pack.addSignature(2, signature);

  ASSERT_EQ(pack.signatures().size(), 3);

  pack.makeHash();
  cs::TransactionsPacket expectedPacket = cs::TransactionsPacket::fromBinary(pack.toBinary());

  cs::Console::writeLine("Pack binary ", cs::Utils::byteStreamToHex(pack.toBinary()));
  cs::Console::writeLine("Expected pack packet binary ", cs::Utils::byteStreamToHex(expectedPacket.toBinary()));

  ASSERT_EQ(pack.toBinary(), expectedPacket.toBinary());
  ASSERT_EQ(pack.signatures().size(), expectedPacket.signatures().size());
  ASSERT_EQ(pack.hash(), expectedPacket.hash());
}
