#pragma once
#include "DefaultIgnore.h"

namespace slv2
{

    class HandleBBState final : public DefaultIgnore
    {
    public:

        ~HandleBBState() override
        {}

        void stateOn(SolverContext& context) override;

        const char * getName() const override
        {
            return "Handle BB";
        }

    };

}
