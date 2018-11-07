#pragma once
#include "DefaultStateBehavior.h"
#include "../CallsQueueScheduler.h"
#include <csdb/address.h>
#include <vector>

namespace slv2
{
    /**
     * @class   NormalState
     *
     * @brief   A normal node state. If spammer mode is on in SolverCore, this state implements spam functionality
     *
     * @author  Alexander Avramenko
     * @date    09.10.2018
     *
     * @sa  T:DefaultStateBehavior  
     *
     * ### remarks  Aae, 30.09.2018.
     */

    class NormalState : public DefaultStateBehavior
    {
    public:

        ~NormalState() override
        {}

        void on(SolverContext& context) override;

        void off(SolverContext& context) override;

        void onRoundEnd(SolverContext& context, bool is_bigbang) override;

        Result onRoundTable(SolverContext& context, const size_t round) override;

        /**
         * @fn  Result NormalState::onBlock(SolverContext& context, csdb::Pool& block, const cs::PublicKey& sender) override;
         *
         * @brief   Overrides base implementation to flush block immediately
         *
         * @author  Alexander Avramenko
         * @date    26.10.2018
         *
         * @param [in,out]  context The context.
         * @param [in,out]  block   The block.
         * @param           sender  The sender.
         *
         * @return  A Result.
         */

        Result onBlock(SolverContext& context, csdb::Pool& block, const cs::PublicKey& sender) override;

        const char * name() const override
        {
            return "Normal";
        }

    protected:

        // returns true if current balance lets spam transactions
        bool check_spammer_balance(SolverContext& context);
        void spam_transaction(SolverContext& context);
		void setup(csdb::Transaction& tr, SolverContext& context);
        void sign(csdb::Transaction& tr);
        int randFT(int min, int max);
        int64_t next_inner_id(size_t round);

        CallsQueueScheduler::CallTag tag_spam { CallsQueueScheduler::no_tag };

        // spammer parameters
        constexpr static uint32_t T_spam_trans = 20;
        constexpr static const size_t CountTransInRound = 1000;
        constexpr static const size_t MinCountTrans = 1;
        
        // every node has unique target spam key
        constexpr static const size_t CountTargetWallets = 1;
        std::vector<csdb::Address> target_wallets;
        
        // every node has unique source key
        constexpr static const size_t PublicKeySize = 32;
        constexpr static const size_t SecretKeySize = 64;
        uint8_t own_secret_key[SecretKeySize];
        csdb::Address own_wallet {};

        bool is_spam_balance_valid { false };
        size_t spam_counter { 0 };
        size_t spam_index { 0 };

        // inner id of last transaction to drain some funds from spammer wallet
        int64_t deposit_inner_id { 0 };
    };

} // slv2
