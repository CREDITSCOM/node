#pragma once

#include <gmock/gmock.h>
#include <vector>

namespace cs
{
    class TransactionsPacket
    {
    public:
        MOCK_CONST_METHOD0(transactions, std::vector<csdb::Transaction>());
        MOCK_METHOD1(addTransaction, bool(const csdb::Transaction&));
        MOCK_CONST_METHOD0(transactionsCount, size_t());
    };

    class Characteristic
    {
    public:
        cs::Bytes mask;
    };

    struct RoundTable
    {
        size_t round = 0;
        PublicKey general;
        std::vector<PublicKey> confidants;
        std::vector<Hash> hashes;
    };

    class Conveyer
    {
    public:

        static Conveyer& instance()
        {
            static Conveyer impl;
            return impl;
        }

        MOCK_CONST_METHOD0(roundTable, RoundTable&());
        MOCK_CONST_METHOD0(transactionsPacketTable, std::set<Hash>&());
        MOCK_CONST_METHOD1(packet, TransactionsPacket&(Hash));
        MOCK_CONST_METHOD1(setCharacteristic, void(const cs::Characteristic&));
    };
}
