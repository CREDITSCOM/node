#pragma once
#include "DefaultStateBehavior.h"
#include <csdb/pool.h>

// Credits::StageOne requires:
#pragma warning(push)
#pragma warning(disable: 4267 4244 4100 4245)
#include <Solver/Solver.hpp>
#pragma warning(pop)

namespace slv2
{
    /**
     * @class   TrustedStage1State
     *
     * @brief   TODO:
     *
     * @author  Alexander Avramenko
     * @date    09.10.2018
     *
     * @sa  T:DefaultStateBehavior  
     *
     * ### remarks  Aae, 30.09.2018.
     */

    class TrustedStage1State : public DefaultStateBehavior
    {
    public:
        
        ~TrustedStage1State() override
        {}

        void on(SolverContext& context) override;

        void off(SolverContext& context) override;

        /**
         * @fn  void TrustedStage1State::onRoundEnd(SolverContext& context, bool is_bigbang) override;
         *
         * @brief   Drops or flushes deferred block depending on big bang
         *
         * @author  Alexander Avramenko
         * @date    26.10.2018
         *
         * @param [in,out]  context     The context.
         * @param           is_bigbang  True if is bigbang, false if not.
         */

        void onRoundEnd(SolverContext& context, bool is_bigbang) override;

        Result onTransactionList(SolverContext& context, csdb::Pool& pool) override;

        Result onHash(SolverContext& context, const Hash& hash, const PublicKey& sender) override;

        const char * name() const override
        {
            return "Trusted-1";
        }

    protected:

        bool enough_hashes { false };
        bool transactions_checked { false };

        Credits::StageOne stage;

        csdb::Pool removeTransactionsWithBadSignatures(SolverContext& context, const csdb::Pool& p);

    };

} // slv2
