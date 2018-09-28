#pragma once

#include "../INodeState.h"

namespace slv2
{

    class DefaultStateBehavior : public INodeState
    {
    public:

        ~DefaultStateBehavior() override
        {}

        // default implementation signals to make transition on Event::RoundTable
        Result onRoundTable(SolverContext& context, const uint32_t round) override;

        // has be invoked from overrides if any
        // default implementation of block handling for almost any state
        Result onBlock(SolverContext& context, csdb::Pool& block, const PublicKey& sender) override;

        Result onVector(SolverContext& context, const Credits::HashVector& vect, const PublicKey& sender) override;

        Result onMatrix(SolverContext& context, const Credits::HashMatrix& matr, const PublicKey& sender) override;

        Result onHash(SolverContext& context, const Hash& hash, const PublicKey& sender) override;

        Result onTransaction(SolverContext& context, const csdb::Transaction& trans) override;

        Result onTransactionList(SolverContext& context, const csdb::Pool& pool) override;

    };

} // slv2
