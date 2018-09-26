#pragma once
#include "DefaultIgnore.h"

namespace slv2
{

    class TrustedVMState final : public DefaultIgnore
    {
    public:

        ~TrustedVMState() override
        {}

        void stateOn(SolverContext& context) override;

        Result onRoundTable(SolverContext& context, const uint32_t round) override;

        const char * getName() const override
        {
            return "TrustedVM";
        }

    private:

        unsigned int m_activation_counter { 0 };
    };

} // slv2
