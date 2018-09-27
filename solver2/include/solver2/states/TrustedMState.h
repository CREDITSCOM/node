#pragma once
#include "DefaultIgnore.h"

namespace slv2
{

    class TrustedMState final : public DefaultIgnore
    {
    public:

        ~TrustedMState() override
        {}

        void on(SolverContext& context) override;

        Result onRoundTable(SolverContext& context, const uint32_t round) override;

        Result onVector(SolverContext& context, const Credits::HashVector& vect, const PublicKey& sender) override;

        const char * name() const override
        {
            return "TrustedM";
        }

    private:

        unsigned int cnt_vectors { 0 };
    };

} // slv2
