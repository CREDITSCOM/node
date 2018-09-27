#pragma once
#include "DefaultIgnore.h"

namespace slv2
{

    class TrustedVState final : public DefaultIgnore
    {
    public:

        ~TrustedVState() override
        {}

        void on(SolverContext& context) override;

        Result onRoundTable(SolverContext& context, const uint32_t round) override;

        Result onMatrix(SolverContext& context, const Credits::HashMatrix& matr, const PublicKey& sender) override;

        const char * name() const override
        {
            return "TrustedV";
        }

    private:

        unsigned int cnt_matrices { 0 };
    };

} // slv2
