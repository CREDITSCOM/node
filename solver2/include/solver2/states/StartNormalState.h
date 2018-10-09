#pragma once

#include "DefaultStateBehavior.h"

namespace slv2
{

    class StartNormalState final : public DefaultStateBehavior
    {
    public:

        ~StartNormalState() override
        {}

        Result onBlock(SolverContext & context, csdb::Pool & block, const PublicKey & sender) override;

        const char * name() const override
        {
            return "StartNormal";
        }
    };

} // slv2
