#pragma once
#include "DefaultIgnore.h"

namespace slv2
{

    class TrustedState final : public DefaultIgnore
    {
    public:
        
        ~TrustedState() override
        {}

        void on(SolverContext& context) override;

        Result onRoundTable(SolverContext& context, const uint32_t round) override;

        Result onVector(SolverContext& context, const Credits::HashVector& vect, const PublicKey& sender) override;

        Result onMatrix(SolverContext& context, const Credits::HashMatrix& matr, const PublicKey& sender) override;

        const char * name() const override
        {
            return "Trusted";
        }

    private:

        unsigned int cnt_vectors { 0 };
        unsigned int cnt_matrices { 0 };
    };

} // slv2
