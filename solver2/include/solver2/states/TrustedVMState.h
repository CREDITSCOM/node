#pragma once
#include "TrustedState.h"

namespace slv2
{

    class TrustedVMState final : public TrustedState
    {
    public:

        ~TrustedVMState() override
        {}

        void on(SolverContext& context) override;

        Result onVector(SolverContext& context, const Credits::HashVector& vect, const PublicKey& sender) override;

        Result onMatrix(SolverContext& context, const Credits::HashMatrix& matr, const PublicKey& sender) override;

        const char * name() const override
        {
            return "TrustedVM";
        }

    private:

        bool decide_to_write(SolverContext& context);
    };

} // slv2
