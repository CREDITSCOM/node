#pragma once
#include "DefaultIgnore.h"

namespace slv2
{

    class TrustedState : public DefaultIgnore
    {
    public:
        
        ~TrustedState() override
        {}

        void on(SolverContext& context) override;

        Result onRoundTable(SolverContext& context, const uint32_t round) override;

        Result onVector(SolverContext& context, const Credits::HashVector& vect, const PublicKey& sender) override;

        Result onMatrix(SolverContext& context, const Credits::HashMatrix& matr, const PublicKey& sender) override;

        Result onBlock(SolverContext& context, const csdb::Pool& pool, const PublicKey& sender) override;

        const char * name() const override
        {
            return "Trusted";
        }

    protected:

        bool test_vectors_completed(const SolverContext& context) const;
        bool test_matrices_completed(const SolverContext& context) const;

        //TODO: уточнить логику блокировки приема матриц после получения блока в тек. раунде
        bool is_block_recv;
    };

} // slv2
