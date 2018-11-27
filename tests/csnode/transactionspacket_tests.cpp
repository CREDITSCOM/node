#define TESTING

#include <gtest/gtest.h>
#include <algorithm>
#include <vector>

#include "transactionspacket.hpp"

#include <csdb/transaction.hpp>
#include <csdb/address.hpp>
#include <csdb/currency.hpp>
#include <sodium.h>

#include <lib/system/utils.hpp>

cs::TransactionsPacket packet;


TEST(TransactionsPacket, createPacket)
{
    ASSERT_TRUE(packet.isHashEmpty());
}

TEST(TransactionsPacket, addTransactions)
{
    auto startAddress = csdb::Address::from_string("0000000000000000000000000000000000000000000000000000000000000007");
    std::vector<uint8_t> myPublicForSig;
    std::vector<uint8_t> myPrivateForSig;
    myPublicForSig.resize(32);
    myPrivateForSig.resize(64);
    crypto_sign_ed25519_keypair(myPublicForSig.data(), myPrivateForSig.data());

    csdb::Transaction transaction;
    transaction.set_target(csdb::Address::from_public_key((char*)myPublicForSig.data()));
    transaction.set_source(startAddress);
    transaction.set_currency(1);
    transaction.set_amount(csdb::Amount(10000, 0));

    const std::size_t randomTransactionsCount = cs::Utils::generateRandomValue(3, 30);
    const std::size_t startInnerID = cs::Utils::generateRandomValue(1, 2789);

    const auto oldtransactionsCount = packet.transactionsCount();

    for (std::size_t i = 0; i < randomTransactionsCount; ++i) {
        transaction.set_innerID(i + startInnerID);
        ASSERT_TRUE(packet.addTransaction(transaction));
    }

    const auto& newtransactionsCount = packet.transactionsCount();
    ASSERT_NE(oldtransactionsCount, newtransactionsCount);
    ASSERT_EQ(newtransactionsCount, randomTransactionsCount);
}

TEST(TransactionsPacket, makeHash)
{
    const auto oldMainHash = packet.hash();

    packet.makeHash();

    ASSERT_FALSE(packet.isHashEmpty());
    ASSERT_NE(oldMainHash, packet.hash());
}

TEST(TransactionsPacket, makeNewHash)
{
    const auto oldMainHash = packet.hash();
    const auto oldtransactionsCount = packet.transactionsCount();

    packet.makeHash();

    ASSERT_EQ(oldMainHash, packet.hash());
    ASSERT_EQ(oldtransactionsCount, packet.transactionsCount());
}

TEST(TransactionsPacket, makeHashFromString)
{
    cs::TransactionsPacketHash hashFromString;
    ASSERT_TRUE(hashFromString.isEmpty());
    const auto& mainHash = packet.hash();
    const std::string mainHashStr = mainHash.toString();
    hashFromString = cs::TransactionsPacketHash::fromString(mainHashStr);

    ASSERT_EQ(mainHash, hashFromString);
    ASSERT_EQ(mainHashStr, hashFromString.toString());
    ASSERT_EQ(mainHash.toBinary(), hashFromString.toBinary());
}

TEST(TransactionsPacket, makeHashFromBinary)
{
    const auto& mainHash = packet.hash();
    const auto& mainHashBin = mainHash.toBinary();
    const cs::TransactionsPacketHash hashFromBinary = cs::TransactionsPacketHash::fromBinary(mainHashBin);
    ASSERT_EQ(mainHash, hashFromBinary);
    ASSERT_EQ(mainHashBin, hashFromBinary.toBinary());
    ASSERT_EQ(mainHash.toString(), hashFromBinary.toString());
}

TEST(TransactionsPacket, makeTransactionsPacketFromBinary)
{
    const auto& mainPacketBin = packet.toBinary();
    const cs::TransactionsPacket packetFromBinary = cs::TransactionsPacket::fromBinary(mainPacketBin);

    ASSERT_EQ(mainPacketBin, packetFromBinary.toBinary());
    ASSERT_EQ(packet.transactionsCount(), packetFromBinary.transactionsCount());

    const auto& mainHash = packet.hash();
    const auto& packetFromBinaryHash = packetFromBinary.hash();

    ASSERT_EQ(mainHash, packetFromBinaryHash);
    ASSERT_EQ(mainHash.toBinary(), packetFromBinaryHash.toBinary());
}

TEST(TransactionsPacket, makeTransactionsPacketFromByteStream)
{
    const auto& mainPacketBin = packet.toBinary();
    const size_t rawSize = mainPacketBin.size();
    const void* rawData = mainPacketBin.data();

    const cs::TransactionsPacket hashFromStream = cs::TransactionsPacket::fromByteStream(static_cast<const char*>(rawData), rawSize);

    ASSERT_EQ(mainPacketBin, hashFromStream.toBinary());
    ASSERT_EQ(packet.transactionsCount(), hashFromStream.transactionsCount());

    const auto& mainHash = packet.hash();
    const auto& hashFromStreamHash = hashFromStream.hash();

    ASSERT_EQ(mainHash, hashFromStreamHash);
    ASSERT_EQ(mainHash.toBinary(), hashFromStreamHash.toBinary());
}

