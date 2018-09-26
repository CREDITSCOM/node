#pragma once
#include "DefaultIgnore.h"

namespace slv2
{

    class TrustedVState final : public DefaultIgnore
    {
    public:

        ~TrustedVState() override
        {}

        void stateOn(SolverContext& context) override;

        Result onRoundTable(SolverContext& context, const uint32_t round) override;

        Result onMatrix(SolverContext& context, const Credits::HashMatrix& matr, const PublicKey& sender) override;

        const char * getName() const override
        {
            return "TrustedV";
        }

    private:

        unsigned int m_cntMatrices { 0 };
    };

} // slv2
