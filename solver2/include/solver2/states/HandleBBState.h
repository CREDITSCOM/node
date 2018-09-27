#pragma once
#include "DefaultIgnore.h"

namespace slv2
{

    class HandleBBState final : public DefaultIgnore
    {
    public:

        ~HandleBBState() override
        {}

        void beforeOn(SolverContext& context) override;

        const char * name() const override
        {
            return "Handle BB";
        }

    };

}
