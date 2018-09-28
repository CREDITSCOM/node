#pragma once
#include "DefaultStateBehavior.h"

namespace slv2
{

    class TrustedState : public DefaultStateBehavior
    {
    public:
        
        ~TrustedState() override
        {}

        void on(SolverContext& context) override;

        Result onRoundTable(SolverContext& context, const uint32_t round) override;

        Result onVector(SolverContext& context, const Credits::HashVector& vect, const PublicKey& sender) override;

        Result onMatrix(SolverContext& context, const Credits::HashMatrix& matr, const PublicKey& sender) override;

        Result onTransactionList(SolverContext& context, const csdb::Pool& pool) override;

        const char * name() const override
        {
            return "Trusted";
        }

    protected:

        bool test_vectors_completed(const SolverContext& context) const;
        bool test_matrices_completed(const SolverContext& context) const;

        // флаг получения списка транзакций в текущем раунде, в начале раунда сбрасывается
        bool is_trans_list_recv { false };

    };

} // slv2
