#pragma once

#include "../INodeState.h"

namespace slv2
{

    class DefaultStateBehavior : public INodeState
    {
    public:

        ~DefaultStateBehavior() override
        {}

        Result onRoundTable(SolverContext& /*context*/, const uint32_t round) override;

        Result onBlock(SolverContext& /*context*/, const csdb::Pool& pool, const PublicKey& /*sender*/) override;

        Result onVector(SolverContext& /*context*/, const Credits::HashVector& /*vect*/, const PublicKey& /*sender*/) override;

        Result onMatrix(SolverContext& /*context*/, const Credits::HashMatrix& /*matr*/, const PublicKey& /*sender*/) override;

        Result onHash(SolverContext& /*context*/, const Hash& /*hash*/, const PublicKey& /*sender*/) override;

        Result onTransaction(SolverContext& /*context*/, const csdb::Transaction& /*trans*/) override;

        Result onTransactionList(SolverContext& /*context*/, const csdb::Pool& /*pool*/) override;

    };

} // slv2
