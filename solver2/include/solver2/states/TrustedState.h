#pragma once
#include "DefaultIgnore.h"

namespace slv2
{

    class TrustedState final : public DefaultIgnore
    {
    public:
        
        ~TrustedState() override
        {}

        void stateOn(SolverContext& context) override;

        Result onRoundTable(SolverContext& context, const uint32_t round) override;

        Result onVector(SolverContext& context, const Credits::HashVector& vect, const PublicKey& sender) override;

        Result onMatrix(SolverContext& context, const Credits::HashMatrix& matr, const PublicKey& sender) override;

        const char * getName() const override
        {
            return "Trusted";
        }

    private:

        unsigned int m_cntVectors { 0 };
        unsigned int m_cntMatrices { 0 };
    };

} // slv2
