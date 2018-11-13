#pragma once
#include "DefaultStateBehavior.h"
#include <Stage.h>

#include <solver/TransactionsValidator.h>
#include <csdb/pool.h>

#include <memory>

namespace cs
{
    class TransactionsPacket;
    class TransactionsValidator;
}

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

        Result onTransactionList(SolverContext& context, cs::TransactionsPacket& pack) override;

        Result onHash(SolverContext& context, const cs::Hash& hash, const cs::PublicKey& sender) override;

        const char * name() const override
        {
            return "Trusted-1";
        }

    protected:

        bool enough_hashes { false };
        bool transactions_checked { false };

        cs::StageOne stage;
        std::unique_ptr<cs::TransactionsValidator> ptransval;

        /**
         * @fn  void TrustedStage1State::filter_test_signatures(SolverContext& context, cs::TransactionsPacket& p);
         *
         * @brief   Filter transactions in packet by testing signatures and remove them
         *
         * @author  Alexander Avramenko
         * @date    13.11.2018
         *
         * @param [in,out]  context The context.
         * @param [in,out]  p       A cs::TransactionsPacket to remove transactions with bad signatures.
         */

        void filter_test_signatures(SolverContext& context, cs::TransactionsPacket& p);
        bool check_transaction_signature(SolverContext& context, const csdb::Transaction& transaction);
        cs::Hash build_vector(SolverContext& context, const cs::TransactionsPacket& trans_pack);

    };

} // slv2
