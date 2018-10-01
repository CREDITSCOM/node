#pragma once
#include "DefaultStateBehavior.h"
#include "Solver/CallsQueueScheduler.h"

namespace slv2
{
    /// <summary>   A normal node state. This class cannot be inherited. </summary>
    ///
    /// <remarks>   Aae, 30.09.2018. </remarks>
    ///
    /// <seealso cref="T:DefaultStateBehavior"/>

    class NormalState final : public DefaultStateBehavior
    {
    public:

        ~NormalState() override
        {}

        void on(SolverContext& context) override;

        void off(SolverContext& context) override;

        Result onBlock(SolverContext& context, csdb::Pool& block, const PublicKey& sender) override;

        const char * name() const override
        {
            return "Normal";
        }

        constexpr static uint32_t T_spam_trans = 20;

    private:

        void setup(csdb::Transaction * ptr, SolverContext * pctx);
        int randFT(int min, int max);

        CallsQueueScheduler::CallTag tag_spam { CallsQueueScheduler::no_tag };
        CallsQueueScheduler::CallTag tag_flush { CallsQueueScheduler::no_tag };
    };

} // slv2
