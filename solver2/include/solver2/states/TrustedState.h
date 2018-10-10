#pragma once
#include "DefaultStateBehavior.h"

namespace slv2
{
    /**
     * @class   TrustedState
     *
     * @brief   A trusted node state. Works itself and inherited by more specific states
     *          (TrustedMState, TrustedVState, TrustedVMState)
     *
     * @author  Alexander Avramenko
     * @date    09.10.2018
     *
     * @sa  T:DefaultStateBehavior  
     *
     * ### remarks  Aae, 30.09.2018.
     */

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

        Result onBlock(SolverContext& context, csdb::Pool& block, const PublicKey& sender) override;

        const char * name() const override
        {
            return "Trusted";
        }

    protected:

        bool test_vectors_completed(const SolverContext& context) const;
        bool test_matrices_completed(const SolverContext& context) const;

    };

} // slv2