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

        //TODO: завершается по логике солвера-1 с приходом блока (см. gotBlock())

        const char * name() const override
        {
            return "Handle BB";
        }

    };

}
