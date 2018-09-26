#pragma once
#include "DefaultIgnore.h"

namespace slv2
{

    class HandleRTState final : public DefaultIgnore
    {
    public:

        ~HandleRTState() override
        {}

        void stateOn(SolverContext& context) override;

        const char * getName() const override
        {
            return "Handle RT";
        }
    };

} // slv2
