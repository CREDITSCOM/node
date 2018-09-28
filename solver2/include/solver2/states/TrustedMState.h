#pragma once
#include "TrustedState.h"

namespace slv2
{

    class TrustedMState final : public TrustedState
    {
    public:

        ~TrustedMState() override
        {}

        void on(SolverContext& context) override;

        // onVector() behaviour is completely implemented in TrustesState

        Result onMatrix(SolverContext& context, const Credits::HashMatrix& matr, const PublicKey& sender) override;

        const char * name() const override
        {
            return "TrustedM";
        }

    };

} // slv2
