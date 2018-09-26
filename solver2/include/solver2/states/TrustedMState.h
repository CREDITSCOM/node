#pragma once
#include "DefaultIgnore.h"

namespace slv2
{

    class TrustedMState final : public DefaultIgnore
    {
    public:

        ~TrustedMState() override
        {}

        void stateOn(SolverContext& context) override;

        Result onRoundTable(SolverContext& context, const uint32_t round) override;

        Result onVector(SolverContext& context, const Credits::HashVector& vect, const PublicKey& sender) override;

        const char * getName() const override
        {
            return "TrustedM";
        }

    private:

        unsigned int m_cntVectors { 0 };
    };

} // slv2
