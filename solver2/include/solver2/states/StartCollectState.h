#pragma once

#include "WriteState.h"

namespace slv2
{
    /**
     * @class   StartCollectState
     *
     * @brief   A start collect state. This class cannot be inherited. To be activated mostly on the
     *          1st round if node level is NodeLevel::Main. Acts as WriteState, sends the first empty
     *          block after timeout, then collect hashes. Also, on round end sends transactions list
     *          to activate consensus on the next round
     *
     * @author  Alexander Avramenko
     * @date    09.10.2018
     *
     * @sa  T:CollectState
     */

    class StartCollectState final: public WriteState
    {
    public:

        ~StartCollectState() override
        {}

        /**
         * @fn  void final::on(SolverContext& context) override;
         *
         * @brief   When on, sends empty block to initiate consensus.
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
         * @brief   When off, cancel scheduled sending of empty block
         *
         * @author  Alexander Avramenko
         * @date    09.10.2018
         *
         * @param [in,out]  context The context.
         */

        void onRoundEnd(SolverContext& context) override;

        const char * name() const override
        {
            return "StartCollect";
        }

    private:

        void cancel_timeout(SolverContext& context);

        CallsQueueScheduler::CallTag tag_timeout { CallsQueueScheduler::no_tag };
    };

} // slv2
