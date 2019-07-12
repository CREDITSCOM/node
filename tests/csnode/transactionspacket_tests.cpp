#define TESTING

#include <gtest/gtest.h>
#include <algorithm>
#include <vector>

#include "transactionspacket.hpp"

#include <cscrypto/cscrypto.hpp>
#include <csdb/address.hpp>
#include <csdb/currency.hpp>
#include <csdb/transaction.hpp>

#include <lib/system/utils.hpp>
#include <lib/system/random.hpp>
#include <lib/system/console.hpp>

static cs::TransactionsPacket testPacket;

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
    ASSERT_TRUE(testPacket.isHashEmpty());
}

TEST(TransactionsPacket, addTransactions) {
    auto startAddress = csdb::Address::from_string("0000000000000000000000000000000000000000000000000000000000000007");
    cs::PublicKey myPublicForSig;

    csdb::Transaction transaction;
    transaction.set_target(csdb::Address::from_public_key(myPublicForSig));
    transaction.set_source(startAddress);
    transaction.set_currency(1);
    transaction.set_amount(csdb::Amount(10000, 0));

    const size_t randomTransactionsCount = cs::Random::generateValue<size_t>(3, 30);
    const size_t startInnerID = cs::Random::generateValue<size_t>(1, 2789);

    const auto oldtransactionsCount = testPacket.transactionsCount();

    for (std::size_t i = 0; i < randomTransactionsCount; ++i) {
        transaction.set_innerID(static_cast<int64_t>(i + startInnerID));
        ASSERT_TRUE(testPacket.addTransaction(transaction));
    }

    const auto& newtransactionsCount = testPacket.transactionsCount();
    ASSERT_NE(oldtransactionsCount, newtransactionsCount);
    ASSERT_EQ(newtransactionsCount, randomTransactionsCount);
}

TEST(TransactionsPacket, makeHash) {
    const auto oldMainHash = testPacket.hash();

    testPacket.makeHash();

    ASSERT_FALSE(testPacket.isHashEmpty());
    ASSERT_NE(oldMainHash, testPacket.hash());
}

TEST(TransactionsPacket, makeNewHash) {
    const auto oldMainHash = testPacket.hash();
    const auto oldtransactionsCount = testPacket.transactionsCount();

    testPacket.makeHash();

    ASSERT_EQ(oldMainHash, testPacket.hash());
    ASSERT_EQ(oldtransactionsCount, testPacket.transactionsCount());
}

TEST(TransactionsPacket, makeHashFromString) {
    cs::TransactionsPacketHash hashFromString;
    ASSERT_TRUE(hashFromString.isEmpty());
    const auto& mainHash = testPacket.hash();
    const std::string mainHashStr = mainHash.toString();
    hashFromString = cs::TransactionsPacketHash::fromString(mainHashStr);

    ASSERT_EQ(mainHash, hashFromString);
    ASSERT_EQ(mainHashStr, hashFromString.toString());
    ASSERT_EQ(mainHash.toBinary(), hashFromString.toBinary());
}

TEST(TransactionsPacket, makeHashFromBinary) {
    const auto& mainHash = testPacket.hash();
    const auto& mainHashBin = mainHash.toBinary();
    const cs::TransactionsPacketHash hashFromBinary = cs::TransactionsPacketHash::fromBinary(mainHashBin);
    ASSERT_EQ(mainHash, hashFromBinary);
    ASSERT_EQ(mainHashBin, hashFromBinary.toBinary());
    ASSERT_EQ(mainHash.toString(), hashFromBinary.toString());
}

TEST(TransactionsPacket, makeTransactionsPacketFromBinary) {
    const auto& mainPacketBin = testPacket.toBinary();
    const cs::TransactionsPacket packetFromBinary = cs::TransactionsPacket::fromBinary(mainPacketBin);

    ASSERT_EQ(mainPacketBin, packetFromBinary.toBinary());
    ASSERT_EQ(testPacket.transactionsCount(), packetFromBinary.transactionsCount());

    const auto& mainHash = testPacket.hash();
    const auto& packetFromBinaryHash = packetFromBinary.hash();

    ASSERT_EQ(mainHash, packetFromBinaryHash);
    ASSERT_EQ(mainHash.toBinary(), packetFromBinaryHash.toBinary());
}

TEST(TransactionsPacket, makeTransactionsPacketFromByteStream) {
    const auto mainPacketBin = testPacket.toBinary();
    const size_t rawSize = mainPacketBin.size();
    const void* rawData = mainPacketBin.data();

    const cs::TransactionsPacket hashFromStream = cs::TransactionsPacket::fromByteStream(static_cast<const char*>(rawData), rawSize);

    ASSERT_EQ(mainPacketBin, hashFromStream.toBinary());
    ASSERT_EQ(testPacket.toBinary(), hashFromStream.toBinary());
    ASSERT_EQ(testPacket.transactionsCount(), hashFromStream.transactionsCount());

    const auto& mainHash = testPacket.hash();
    const auto& hashFromStreamHash = hashFromStream.hash();

    ASSERT_EQ(mainHash, hashFromStreamHash);
    ASSERT_EQ(mainHash.toBinary(), hashFromStreamHash.toBinary());
}

TEST(TransactionsPacket, signaturesSerialization) {
    constexpr size_t maxTransactionsCount = 100;
    const size_t count = cs::Random::generateValue<size_t>(0, maxTransactionsCount);
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

TEST(TransactionsPacket, copyConstructor) {
    cs::TransactionsPacket packet;

    for (int64_t i = 0; i < 100; ++i) {
        packet.addTransaction(makeTransaction(i));
    }

    cs::TransactionsPacket copiedPacket = packet;
    ASSERT_EQ(copiedPacket.toBinary(), packet.toBinary());
}

TEST(TransactionsPacket, moveConstructor) {
    cs::TransactionsPacket packet;

    for (int64_t i = 0; i < 100; ++i) {
        packet.addTransaction(makeTransaction(i));
    }

    cs::TransactionsPacket copiedPacket = packet;
    cs::TransactionsPacket movedPacket = std::move(packet);

    ASSERT_EQ(copiedPacket.toBinary(), movedPacket.toBinary());
}

TEST(TransactionsPacket, copyOperator) {
    cs::TransactionsPacket packet;

    for (int64_t i = 0; i < 100; ++i) {
        packet.addTransaction(makeTransaction(i));
    }

    cs::TransactionsPacket copiedPacket;
    copiedPacket = packet;

    ASSERT_EQ(copiedPacket.toBinary(), packet.toBinary());
}

TEST(TransactionsPacket, moveOperator) {
    cs::TransactionsPacket packet;

    for (int64_t i = 0; i < 100; ++i) {
        packet.addTransaction(makeTransaction(i));
    }

    cs::TransactionsPacket copiedPacket = packet;
    cs::TransactionsPacket movedPacket;
    movedPacket = std::move(packet);

    ASSERT_EQ(copiedPacket.toBinary(), movedPacket.toBinary());
}

TEST(TransactionPacketHash, fromBinary) {
    auto startAddress = csdb::Address::from_string("0000000000000000000000000000000000000000000000000000000000000007");

    cs::PublicKey myPublicForSig;
    myPublicForSig.fill(0);

    csdb::Transaction transaction;
    transaction.set_target(csdb::Address::from_public_key(myPublicForSig));
    transaction.set_source(startAddress);
    transaction.set_currency(1);
    transaction.set_amount(csdb::Amount(10000, 0));
    transaction.set_innerID(cs::Random::generateValue<int64_t>(1, 2789));

    cs::TransactionsPacket packet;
    packet.addTransaction(transaction);

    ASSERT_EQ(packet.makeHash(), true);

    auto bytes = packet.hash().toBinary();
    cs::TransactionsPacketHash hash = cs::TransactionsPacketHash::fromBinary(std::move(bytes));

    ASSERT_EQ(hash, packet.hash());
}
