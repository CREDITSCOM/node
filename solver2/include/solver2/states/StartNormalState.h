#pragma once

#include "DefaultStateBehavior.h"
#include "../CallsQueueScheduler.h"

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

        void on(SolverContext& context) override;

        void onRoundEnd(SolverContext& context) override;

        Result onBlock(SolverContext & context, csdb::Pool & block, const PublicKey & sender) override;

        const char * name() const override
        {
            return "StartNormal";
        }

    private:

        CallsQueueScheduler::CallTag tag_timeout {CallsQueueScheduler::no_tag};

        void cancel_timeout(SolverContext& context);
    };

} // slv2
