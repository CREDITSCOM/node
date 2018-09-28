#pragma once
#include "DefaultStateBehavior.h"

namespace slv2
{

    class StartState final : public DefaultStateBehavior
    {
    public:

        ~StartState() override
        {}

        void on(SolverContext& context) override;

        Result onRoundTable(SolverContext& context, const uint32_t round) override;

        const char * name() const override
        {
            return "Start";
        }

    };

} // slv2
