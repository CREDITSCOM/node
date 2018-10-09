#pragma once

#include "CollectState.h"

namespace slv2
{
    /**
     * @class   StartCollectState
     *
     * @brief   A start collect state. This class cannot be inherited. To be activated mostly on the
     *          1st round if node level is NodeLevel::Main
     *
     * @author  Alexander Avramenko
     * @date    09.10.2018
     *
     * @sa  T:CollectState
     */

    class StartCollectState final: public CollectState
    {
    public:

        ~StartCollectState() override
        {}

        /**
         * @fn  void final::on(SolverContext& context) override;
         *
         * @brief   When on, sends empty transaction list to initiate consensus.
         *
         * @author  Alexander Avramenko
         * @date    09.10.2018
         *
         * @param [in,out]  context The context.
         */

        void on(SolverContext& context) override;

        /**
         * @fn  void final::onRoundEnd(SolverContext& context) override;
         *
         * @brief   When off, cancel scheduled sending of empty transaction list
         *
         * @author  Alexander Avramenko
         * @date    09.10.2018
         *
         * @param [in,out]  context The context.
         */

        void onRoundEnd(SolverContext& context) override;

        /**
         * @fn  Result final::onBlock(SolverContext& context, csdb::Pool& block, const PublicKey& sender) override;
         *
         * @brief   Executes the base class block action. Also cancel scheduled sending of empty transaction list if any.
         *          Receiving a block means that consensus in current round has held
         *          
         *
         * @author  Alexander Avramenko
         * @date    09.10.2018
         *
         * @param [in,out]  context The context.
         * @param [in,out]  block   The block.
         * @param           sender  The sender.
         *
         * @return  A base class method result.
         */

        Result onBlock(SolverContext& context, csdb::Pool& block, const PublicKey& sender) override;

        const char * name() const override
        {
            return "StartCollect";
        }

    private:

        void cancel_timeout(SolverContext& context);

        CallsQueueScheduler::CallTag tag_timeout { CallsQueueScheduler::no_tag };
    };

} // slv2
