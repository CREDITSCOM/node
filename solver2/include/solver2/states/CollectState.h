#pragma once
#include "DefaultStateBehavior.h"

#include <csdb/pool.h>

namespace slv2
{
    /**
     * @class   CollectState
     *
     * @brief   A transaction collector node state (so called "main node").
     *
     * @author  aae
     * @date    02.10.2018
     *
     * @sa  T:DefaultStateBehavior  
     *
     * ### remarks  Aae, 30.09.2018.
     */

    class CollectState : public DefaultStateBehavior
    {
    public:

        ~CollectState() override
        {}

        void on(SolverContext& context) override;

        /**
         * @fn  virtual void final::onRoundEnd(SolverContext& context) override;
         *
         * @brief   Cancel round timeout if set. Sends list of transactions collected this round
         *
         * @author  aae
         * @date    01.10.2018
         *
         * @param [in,out]  context The context.
         */

        void onRoundEnd(SolverContext& context) override;

        Result onTransaction(SolverContext& context, const csdb::Transaction& tr) override;

        const char * name() const override
        {
            return "Collect";
        }

    protected:

        size_t cnt_transactions { 0 };
        csdb::Pool pool {};

        void do_send_tl(SolverContext& context, uint64_t sequence);
    };

} // slv2
