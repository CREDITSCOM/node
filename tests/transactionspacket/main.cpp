#define BOOST_TEST_MODULE CsNodeTests

#include <iostream>
#include <boost/test/unit_test.hpp>
#include <csnode/transactionspacket.h>
#include <csdb/transaction.h>
#include <csdb/address.h>
#include <csdb/currency.h>
#include <sodium.h>

/*
    Tests server
*/
BOOST_AUTO_TEST_SUITE(CsNodeTests)

    /*
        Tests server
    */
    BOOST_AUTO_TEST_CASE(MainServerTests)
    {
        std::cout << "\nLet the magic begin!\n\n";

        cs::TransactionsPacket tp;

        // test hash
        std::cout << "create packet. is hash empty: " << std::boolalpha << tp.isHashEmpty() << "\n\n";
        BOOST_CHECK(tp.isHashEmpty());

        std::cout << "make hash: " << "\n";
        tp.makeHash();
        std::cout << "hash not empty: " << std::boolalpha << !tp.isHashEmpty() << "\n";
        BOOST_CHECK(!tp.isHashEmpty());

        // hash to String
        auto & hash = tp.hash();
        std::string hashStr = hash.toString();
        std::cout << "hash.toString() representation \n";
        std::cout << "hash size    : " << hash.size() << "\n";
        std::cout << "hash         : " << hashStr << "\n\n";

        // make hash from String
        cs::TransactionsPacketHash tph;
        BOOST_CHECK(tph.isEmpty());
        tph = cs::TransactionsPacketHash::fromString(hashStr);
        std::cout << "make new hash from old hash.toString() representation \n";
        std::cout << "NewHash size : " << tph.size() << "\n";
        std::cout << "NewHash      : " << tph.toString() << "\n\n";
        BOOST_CHECK(hash == tph);

        // make hash from Binary
        cs::TransactionsPacketHash binHash = cs::TransactionsPacketHash::fromBinary(hash.toBinary());
        std::cout << "make new hash from old fromBinary(hash.toBinary()) representation \n";
        std::cout << "binHash size : " << binHash.size() << "\n";
        std::cout << "binHash      : " << binHash.toString() << "\n\n";
        BOOST_CHECK(hash.toBinary() == binHash.toBinary());

        // test transactions
        std::cout << "old transactionsCount: " << tp.transactionsCount() << "\n";

        auto start_address = csdb::Address::from_string("0000000000000000000000000000000000000000000000000000000000000007");
        std::vector<uint8_t> myPublicForSig;
        std::vector<uint8_t> myPrivateForSig;
        myPublicForSig.resize(32);
        myPrivateForSig.resize(64);
        crypto_sign_ed25519_keypair(myPublicForSig.data(), myPrivateForSig.data());

        csdb::Transaction transaction;
        transaction.set_target(csdb::Address::from_public_key((char*)myPublicForSig.data()));
        transaction.set_source(start_address);

        transaction.set_currency(1);
        transaction.set_amount(csdb::Amount(10000, 0));

        for (int i = 0; i < 15; ++i)
        {
            transaction.set_innerID(i + 2789);
            tp.addTransaction(transaction);
        }

        // test make hash
        {
            auto oldhash = tp.hash();
            std::cout << "old hash     : " << oldhash.toString() << "\n";
            tp.makeHash();
            auto & newhash = tp.hash();
            std::cout << "new transactionsCount: " << tp.transactionsCount() << "\n";
            std::cout << "new hash     : " << newhash.toString() << "\n\n";
            BOOST_CHECK(oldhash == newhash);
        }

        // create TransactionsPacket from binary
        cs::TransactionsPacket tpBin     = cs::TransactionsPacket::fromBinary(tp.toBinary());
        auto &                 tpBinHash = tpBin.hash();
        std::cout << "tpBin trans  : " << tpBin.transactionsCount() << "\n";
        std::cout << "tpBin hash s : " << tpBinHash.size() << "\n";
        std::cout << "tpBin hash   : " << tpBinHash.toString() << "\n\n";
        BOOST_CHECK(tp.transactionsCount() == tpBin.transactionsCount());
        BOOST_CHECK(tp.hash()              != tpBin.hash());

        // test binary stream
        cs::Bytes buffer  = tp.toBinary();
        const size_t        rawSize = buffer.size();
        void*               rawData = buffer.data();
        std::cout << "tp    rawData Size : " << rawSize << "\n";
        std::cout << "tpBin rawData Size : " << tpBin.toBinary().size() << "\n\n";

        // create TransactionsPacket from Byte Stream
        cs::TransactionsPacket tpBinStream    = cs::TransactionsPacket::fromByteStream(static_cast<const char*>(rawData), rawSize);
        auto &                 tpBinStremHash = tpBinStream.hash();
        std::cout << "tpBinStream trans  : " << tpBinStream.transactionsCount() << "\n";
        std::cout << "tpBinStream hash s : " << tpBinStremHash.size() << "\n";
        std::cout << "tpBinStream hash   : " << tpBinStremHash.toString() << "\n\n";
        BOOST_CHECK(tp.transactionsCount() == tpBinStream.transactionsCount());
        BOOST_CHECK(tpBin.hash()           == tpBinStream.hash());
        BOOST_CHECK(tp.hash()              != tpBinStream.hash());

    }

BOOST_AUTO_TEST_SUITE_END()
