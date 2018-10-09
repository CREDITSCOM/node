#pragma once
#include "DefaultStateBehavior.h"
#include "../CallsQueueScheduler.h"
#include <csdb/address.h>
#include <vector>

namespace slv2
{
    /// <summary>   A normal node state. This class cannot be inherited. </summary>
    ///
    /// <remarks>   Aae, 30.09.2018. </remarks>
    ///
    /// <seealso cref="T:DefaultStateBehavior"/>

    class NormalState : public DefaultStateBehavior
    {
    public:

        ~NormalState() override
        {}

        void on(SolverContext& context) override;

        void off(SolverContext& context) override;

        void onRoundEnd(SolverContext& context) override;

        Result onRoundTable(SolverContext& context, const uint32_t round) override;

        Result onBlock(SolverContext& context, csdb::Pool& block, const PublicKey& sender) override;

        const char * name() const override
        {
            return "Normal";
        }


    protected:

        void check_spammer_balance(SolverContext& context);
		void setup(csdb::Transaction * ptr, SolverContext * pctx);
        int randFT(int min, int max);

        CallsQueueScheduler::CallTag tag_spam { CallsQueueScheduler::no_tag };
        CallsQueueScheduler::CallTag tag_flush { CallsQueueScheduler::no_tag };

        constexpr static uint32_t T_spam_trans = 20;

        constexpr static const size_t CountTransInRound = 100;
        // every node has unique target spam key
        constexpr static const size_t CountTargetWallets = 1;
        std::vector<csdb::Address> target_wallets;
        // every node has unique source key
        csdb::Address own_wallet {};
        size_t spam_counter { 0 };
        size_t spam_index { 0 };

        // counts flushed transactions during round
        size_t flushed_counter { 0 };
    };

} // slv2
