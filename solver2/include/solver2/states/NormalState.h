#pragma once
#include "DefaultIgnore.h"
#include "Solver/CallsQueueScheduler.h"

namespace slv2
{

    class NormalState final : public DefaultIgnore
    {
    public:

        ~NormalState() override
        {}

        void on(SolverContext& context) override;

        void off(SolverContext& context) override;

        Result onRoundTable(SolverContext& context, const uint32_t round) override;

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
