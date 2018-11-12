#define BOOST_TEST_MODULE CsNodeTests

#include <iostream>
#include <boost/test/unit_test.hpp>
#include <csnode/transactionspacket.hpp>
#include <csdb/transaction.h>
#include <csdb/address.h>
#include <csdb/currency.h>
#include <sodium.h>

#include <lib/system/logger.hpp>
#include <lib/system/utils.hpp>

/*
    Tests cs node
*/
BOOST_AUTO_TEST_SUITE(CsNodeTests)

    /*
        Tests transactions packet
    */
    BOOST_AUTO_TEST_CASE(TransactionsPacketTests)
    {
        cs::TransactionsPacket packet;

        // test hash
        cslog() << "Create packet. Is hash empty: " << packet.isHashEmpty();
        BOOST_CHECK(packet.isHashEmpty());

        // test transactions
        const auto startMainHash = packet.hash();
        const auto startMainTransactionsCount = packet.transactionsCount();
        cslog() << "Main transactions count: " << startMainTransactionsCount;
        cslog() << "Start hash     : " << startMainHash.toString();

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
        cslog() << "Generate transactions count: " << randomTransactionsCount;

        for (std::size_t i = 0; i < randomTransactionsCount; ++i)
        {
            transaction.set_innerID(i + startInnerID);
            BOOST_CHECK(packet.addTransaction(transaction));
        }

        // test make hash
        packet.makeHash();
        cslog() << "Make hash: " << !packet.isHashEmpty();
        BOOST_CHECK(!packet.isHashEmpty());
        const auto mainHash = packet.hash();
        const auto& maintransactionsCount = packet.transactionsCount();
        cslog() << "New transactions count: " << maintransactionsCount;
        cslog() << "New hash size : " << mainHash.size();
        cslog() << "New hash : " << mainHash.toString();
        BOOST_CHECK(startMainHash != mainHash);
        BOOST_CHECK(startMainTransactionsCount != maintransactionsCount);

        packet.makeHash();
        BOOST_CHECK(mainHash == packet.hash());

        // hash to String
        const std::string mainHashStr = mainHash.toString();
        cslog() << "Main hash string representation: " << mainHashStr;

        const auto& mainHashBin = mainHash.toBinary();

        // make hash from String
        cs::TransactionsPacketHash hashFromString;
        BOOST_CHECK(hashFromString.isEmpty());
        hashFromString = cs::TransactionsPacketHash::fromString(mainHashStr);
        cslog() << "Make new hash from string representation: " << hashFromString.toString();
        cslog() << "Hash from string size : " << hashFromString.size();
        BOOST_CHECK(mainHash == hashFromString);
        BOOST_CHECK(mainHashBin == hashFromString.toBinary());

        // make hash from Binary
        cs::TransactionsPacketHash hashFromBinary = cs::TransactionsPacketHash::fromBinary(mainHashBin);
        cslog() << "Make new hash from binary representation: " << hashFromBinary.toString();
        cslog() << "Hash from binary size : " << hashFromBinary.size();
        BOOST_CHECK(mainHash == hashFromBinary);
        BOOST_CHECK(mainHashBin == hashFromBinary.toBinary());

        const auto& mainPacketBin = packet.toBinary();

        // create TransactionsPacket from binary
        cs::TransactionsPacket packetFromBinary = cs::TransactionsPacket::fromBinary(packet.toBinary());
        const auto& packetFromBinaryHash = packetFromBinary.hash();
        cslog() << "Packet from binary transactions count: " << packetFromBinary.transactionsCount();
        cslog() << "Packet from binary hash size: " << packetFromBinaryHash.size();
        cslog() << "Packet from binary hash string: " << packetFromBinaryHash.toString();
        BOOST_CHECK(packet.transactionsCount() == packetFromBinary.transactionsCount());
        BOOST_CHECK(mainHash == packetFromBinaryHash);
        BOOST_CHECK(mainHashBin == packetFromBinaryHash.toBinary());
        BOOST_CHECK(mainPacketBin == packetFromBinary.toBinary());

        // test binary stream
        const size_t rawSize = mainPacketBin.size();
        const void* rawData = mainPacketBin.data();
        cslog() << "Main packet binary size : " << rawSize;

        // create TransactionsPacket from Byte Stream
        cs::TransactionsPacket hashFromStream = cs::TransactionsPacket::fromByteStream(static_cast<const char*>(rawData), rawSize);
        const auto& hashFromStreamHash = hashFromStream.hash();
        cslog() << "Packet from stream transactions count: " << hashFromStream.transactionsCount();
        cslog() << "Packet from stream hash size: " << hashFromStreamHash.size();
        cslog() << "Packet from stream hash string: " << hashFromStreamHash.toString();
        BOOST_CHECK(packet.transactionsCount() == hashFromStream.transactionsCount());
        BOOST_CHECK(mainHash == hashFromStreamHash);
        BOOST_CHECK(packetFromBinaryHash == hashFromStreamHash);
        BOOST_CHECK(mainHashBin == hashFromStreamHash.toBinary());
        BOOST_CHECK(mainPacketBin == hashFromStream.toBinary());

    }

BOOST_AUTO_TEST_SUITE_END()
