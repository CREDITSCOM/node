#pragma once

#include "DefaultStateBehavior.h"

namespace slv2
{
    /**
     * @class   StartNormalState
     *
     * @brief   A start normal state. This class cannot be inherited. To be activated mostly on the
     *          1st round if node level is NodeLevel::Normal
     *
     * @author  Alexander Avramenko
     * @date    09.10.2018
     *
     * @sa  T:DefaultStateBehavior
     */

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
