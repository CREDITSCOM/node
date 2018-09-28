#pragma once
#include "DefaultStateBehavior.h"

namespace slv2
{

    class HandleRTState final : public DefaultStateBehavior
    {
    public:

        ~HandleRTState() override
        {}

        void on(SolverContext& context) override;

        const char * name() const override
        {
            return "Handle RT";
        }
    };

} // slv2
