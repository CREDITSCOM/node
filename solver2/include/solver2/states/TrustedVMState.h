#pragma once
#include "DefaultIgnore.h"

namespace slv2
{

    class TrustedVMState final : public DefaultIgnore
    {
    public:

        ~TrustedVMState() override
        {}

        void beforeOn(SolverContext& context) override;

        Result onRoundTable(SolverContext& context, const uint32_t round) override;

        const char * name() const override
        {
            return "TrustedVM";
        }

    private:

        unsigned int activation_counter { 0 };
    };

} // slv2
