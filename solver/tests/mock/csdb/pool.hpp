#pragma once

#include <gmock/gmock.h>
#include <vector>
#include <cstdint>
#include <lib/system/common.hpp>

namespace csdb
{
    class PoolHash
    {
    public:

        PoolHash() = default;
        PoolHash(const PoolHash&)
        {}

        MOCK_METHOD0(to_binary, cs::Bytes&());
    };

    class Address
    {
    public:
        using WalletId = uint32_t;

        Address() = default;
        Address(const Address&)
        {}

        MOCK_CONST_METHOD0(is_wallet_id, bool());
        MOCK_CONST_METHOD0(wallet_id, WalletId());
        MOCK_CONST_METHOD0(public_key, const cs::Bytes&());
    };

    class Amount
    {
    public:
        Amount() = default;
        Amount(const Amount&)
        {}

        MOCK_CONST_METHOD0(to_double, double());
    };

    class Transaction
    {
    public:
        Transaction() = default;
        Transaction(const Transaction&)
        {}

        MOCK_CONST_METHOD0(innerID, int64_t());
        MOCK_CONST_METHOD0(source, Address());
        MOCK_CONST_METHOD0(target, Address());
        MOCK_CONST_METHOD0(amount, Amount());
        MOCK_CONST_METHOD1(verify_signature, bool(cs::Bytes));
    };

    class Pool
    {
    public:
        Pool() = default;

        Pool(const Pool&)
        {}

        Pool& operator=(const Pool&)
        {
            return *this;
        }

        MOCK_CONST_METHOD0(sequence, size_t());
        MOCK_METHOD1(set_sequence, void(size_t));
        MOCK_CONST_METHOD0(transactions_count, size_t());
        MOCK_CONST_METHOD0(transactions, const std::vector<Transaction>());
        MOCK_METHOD0(verify_signature, bool());
        MOCK_METHOD1(add_transaction, void(const Transaction));
        MOCK_CONST_METHOD0(writer_public_key, cs::Bytes());
    };

}
