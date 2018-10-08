#pragma once
#include "INodeState.h"

namespace slv2
{
    /// <summary>   A special state of "No state". Used at the very beginning of work. This class cannot be inherited. </summary>
    ///
    /// <remarks>   Aae, 30.09.2018. </remarks>
    ///
    /// <seealso cref="T:INodeState"/>

    class NoState final : public INodeState
    {
    public:

        virtual void on(SolverContext& /*context*/) override
        {}

        virtual void off(SolverContext& /*context*/) override
        {}

        virtual void expired(SolverContext& /*context*/) override
        {}

        virtual void onRoundEnd(SolverContext& /*context*/) override
        {}

        Result onRoundTable(SolverContext& /*context*/, const uint32_t /*round*/) override
        {
            return Result::Finish;
        }

        Result onBlock(SolverContext& /*context*/, csdb::Pool& /*pool*/, const PublicKey& /*sender*/) override
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

        const char * name() const override
        {
            return "None";
        }

    };

} // slv2
