#pragma once
#include "DefaultIgnore.h"

namespace slv2
{

    class NormalState final : public DefaultIgnore
    {
    public:

        ~NormalState() override
        {}

        Result onRoundTable(SolverContext& context, const uint32_t round) override;

        const char * getName() const override
        {
            return "Normal";
        }
    };

} // slv2
