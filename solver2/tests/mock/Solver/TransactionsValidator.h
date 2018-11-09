#pragma once
#include <gmock/gmock.h>
#include <csdb/pool.h>
#include <Solver/WalletsState.h>

namespace cs
{

    class TransactionsValidator
    {
    public:

        struct Config
        {};

        TransactionsValidator(const cs::WalletsState&, const Config&)
        {}

        MOCK_METHOD1(reset, void(size_t));
        MOCK_METHOD3(validateTransaction, bool(csdb::Transaction, size_t, uint8_t&));
        MOCK_METHOD3(validateByGraph, void(cs::Bytes&, const std::vector<csdb::Transaction>&, const csdb::Pool&));
    };

}
