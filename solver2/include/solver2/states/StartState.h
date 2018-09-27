#pragma once
#include "DefaultIgnore.h"

namespace slv2
{

    class StartState final : public DefaultIgnore
    {
    public:

        ~StartState() override
        {}

        Result onRoundTable(SolverContext& context, const uint32_t round) override;

        const char * name() const override
        {
            return "Start";
        }

    };

} // slv2
