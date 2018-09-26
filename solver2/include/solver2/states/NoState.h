#pragma once
#include "INodeState.h"

namespace slv2
{
    class NoState final : public INodeState
    {
    public:

        //~NoState() override
        //{}

        Result onRoundTable(SolverContext& /*context*/, const uint32_t /*round*/) override
        {
            return Result::Failure;
        }

        Result onBlock(SolverContext& /*context*/, const csdb::Pool& /*pool*/, const PublicKey& /*sender*/) override
        {
            return Result::Failure;
        }

        Result onVector(SolverContext& /*context*/, const Credits::HashVector& /*vect*/, const PublicKey& /*sender*/) override
        {
            return Result::Failure;
        }

        Result onMatrix(SolverContext& /*context*/, const Credits::HashMatrix& /*matr*/, const PublicKey& /*sender*/) override
        {
            return Result::Failure;
        }

        Result onHash(SolverContext& /*context*/, const Hash& /*hash*/, const PublicKey& /*sender*/) override
        {
            return Result::Failure;
        }

        Result onTransaction(SolverContext& /*context*/, const csdb::Transaction& /*trans*/) override
        {
            return Result::Failure;
        }

        Result onTransactionList(SolverContext& /*context*/, const csdb::Pool& /*pool*/) override
        {
            return Result::Failure;
        }

        const char * getName() const override
        {
            return "None";
        }

    };

} // slv2
