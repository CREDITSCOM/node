#pragma once

#include "../INodeState.h"

namespace slv2
{
    /**
     * @class   DefaultStateBehavior
     *
     * @brief   Implements a default node state behavior. Intended to be inherited by most of all
     *          final states classes
     *
     * @author  aae
     * @date    01.10.2018
     *
     * @sa  T:INodeState    
     *
     * ### remarks  Aae, 30.09.2018.
     */

    class DefaultStateBehavior : public INodeState
    {
    public:

        ~DefaultStateBehavior() override
        {}

        void on(SolverContext& /*context*/) override
        {}

        void off(SolverContext& /*context*/) override
        {}

        void expired(SolverContext& /*context*/) override
        {}

        /**
         * @fn  void DefaultStateBehavior::onRoundEnd(SolverContext& context, bool is_bigbang) override;
         *
         * @brief   Executes the round end action: stores block if write was deferred
         *
         * @author  Alexander Avramenko
         * @date    24.10.2018
         *
         * @param [in,out]  context     The context.
         * @param           is_bigbang  True if is bigbang, false if not.
         */

        void onRoundEnd(SolverContext& context, bool is_bigbang) override;

        /**
         * @fn  Result DefaultStateBehavior::onRoundTable(SolverContext& context, const uint32_t round, bool is_bigbang) override;
         *
         * @brief   Executes the round table action. Signals for core to make transition on
         *          Event::RoundTable
         *
         * @author  aae
         * @date    01.10.2018
         *
         * @param [in,out]  context     The context.
         * @param           round       The new round number.
         * @param           is_bigbang  True if is bigbang, false if not.
         *
         * @return  A Result::Finished value.
         */

        Result onRoundTable(SolverContext& context, const size_t round) override;

        /**
         * @fn  Result DefaultStateBehavior::onBlock(SolverContext& context, csdb::Pool& block, const cs::PublicKey& sender) override;
         *
         * @brief   Do test of block received.
         *          
         *          If OK stores block in chain storage. Has to be invoked in overrides.
         *          Performs deferred block write. May be overridden to flush block immediately.
         *
         * @author  aae
         * @date    01.10.2018
         *
         * @param [in,out]  context The context.
         * @param [in,out]  block   The block received.
         * @param           sender  The sender of current block.
         *
         * @return  A Result::Finish value if block accepted and stored, Result::Ignore value if ignored.
         *
         * ### remarks  Aae, 30.09.2018.
         */

        Result onBlock(SolverContext& context, csdb::Pool& block, const cs::PublicKey& sender) override;

        /**
         * @fn  Result DefaultStateBehavior::onHash(SolverContext& context, const cs::Hash& hash, const cs::PublicKey& sender) override;
         *
         * @brief   Ignores the hash received
         *
         * @author  aae
         * @date    01.10.2018
         *
         * @param [in,out]  context The context.
         * @param           hash    The hash.
         * @param           sender  The sender.
         *
         * @return  A Result::Ignore value.
         *
         * ### remarks  Aae, 30.09.2018.
         */

        Result onHash(SolverContext& context, const cs::Hash& hash, const cs::PublicKey& sender) override;

        /**
         * @fn  Result DefaultStateBehavior::onTransaction(SolverContext& context, const csdb::Transaction& trans) override;
         *
         * @brief   Ignores the transaction received
         *
         * @author  aae
         * @date    01.10.2018
         *
         * @param [in,out]  context The context.
         * @param           trans   The transaction.
         *
         * @return  A Result::Ignore value.
         *
         * ### remarks  Aae, 30.09.2018.
         */

        Result onTransaction(SolverContext& context, const csdb::Transaction& trans) override;

        /**
         * @fn  Result DefaultStateBehavior::onTransactionList(SolverContext& context, const csdb::Pool& pool) override;
         *
         * @brief   Ignores the transaction list received
         *
         * @author  aae
         * @date    01.10.2018
         *
         * @param [in,out]  context The context.
         * @param           pool    The pool.
         *
         * @return  A Result::Ignore value.
         *
         * ### remarks  Aae, 30.09.2018.
         */

        Result onTransactionList(SolverContext& context, csdb::Pool& pool) override;

        Result onStage1(SolverContext& context, const cs::StageOne& stage) override;
        Result onStage2(SolverContext& context, const cs::StageTwo& stage) override;
        Result onStage3(SolverContext& context, const cs::StageThree& stage) override;

    protected:

        /**
         * @fn  void DefaultStateBehavior::sendLastWrittenHash(SolverContext& context, const cs::PublicKey& sender);
         *
         * @brief   Sends a last written hash to 
         *
         * @author  Alexander Avramenko
         * @date    10.10.2018
         *
         * @param [in,out]  context The context.
         * @param           target  The target receiver of hash sent.
         */

        void sendLastWrittenHash(SolverContext& context, const cs::PublicKey& target);
    };

} // slv2
