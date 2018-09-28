#pragma once
#include "DefaultStateBehavior.h"

namespace slv2
{

    class HandleBBState final : public DefaultStateBehavior
    {
    public:

        ~HandleBBState() override
        {}

        void on(SolverContext& context) override;

        const char * name() const override
        {
            return "Handle BB";
        }

    };

}
