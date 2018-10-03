#pragma once

#include "../INodeState.h"
#include <vector>

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

        virtual void on(SolverContext& /*context*/) override
        {
            report_ignore_transactions = true;
        }

        virtual void off(SolverContext& /*context*/) override
        {
            if(!future_blocks.empty()) {
                future_blocks.clear();
            }
        }

        virtual void expired(SolverContext& /*context*/) override
        {}

        virtual void onRoundEnd(SolverContext& /*context*/) override
        {}

        /**
         * @fn  Result DefaultStateBehavior::onRoundTable(SolverContext& context, const uint32_t round) override;
         *
         * @brief   Executes the round table action. Signals for core to make transition on
         *          Event::RoundTable
         *
         * @author  aae
         * @date    01.10.2018
         *
         * @param [in,out]  context The context.
         * @param           round   The new round number.
         *
         * @return  A Result::Finished value.
         *
         * ### remarks  Aae, 30.09.2018.
         */

        Result onRoundTable(SolverContext& context, const uint32_t round) override;

        /**
         * @fn  Result DefaultStateBehavior::onBlock(SolverContext& context, csdb::Pool& block, const PublicKey& sender) override;
         *
         * @brief   Do test of block received.
         *          
         *          If OK stores block in chain storage. Must be overridden to send hash back to sender. Has to be invoked in overrides.
         *          If overrides send hash of last block back to sender they MUST use last_block_sender protected data member instead of
         *          method argument. It is caused by possible restoring of cached blocks
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

        Result onBlock(SolverContext& context, csdb::Pool& block, const PublicKey& sender) override;

        /**
         * @fn  Result DefaultStateBehavior::onVector(SolverContext& context, const Credits::HashVector& vect, const PublicKey& sender) override;
         *
         * @brief   Ignores vector received
         *
         * @author  aae
         * @date    01.10.2018
         *
         * @param [in,out]  context The context.
         * @param           vect    The vect.
         * @param           sender  The sender.
         *
         * @return  A Result::Ignore value.
         *
         * ### remarks  Aae, 30.09.2018.
         */

        Result onVector(SolverContext& context, const Credits::HashVector& vect, const PublicKey& sender) override;

        /**
         * @fn  Result DefaultStateBehavior::onMatrix(SolverContext& context, const Credits::HashMatrix& matr, const PublicKey& sender) override;
         *
         * @brief   Ignores the matrix received
         *
         * @author  aae
         * @date    01.10.2018
         *
         * @param [in,out]  context The context.
         * @param           matr    The matr.
         * @param           sender  The sender.
         *
         * @return  A Result::Ignore value.
         *
         * ### remarks  Aae, 30.09.2018.
         */

        Result onMatrix(SolverContext& context, const Credits::HashMatrix& matr, const PublicKey& sender) override;

        /**
         * @fn  Result DefaultStateBehavior::onHash(SolverContext& context, const Hash& hash, const PublicKey& sender) override;
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

        Result onHash(SolverContext& context, const Hash& hash, const PublicKey& sender) override;

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

        Result onTransactionList(SolverContext& context, const csdb::Pool& pool) override;

    protected:

        /** @brief   Flag to suppress too much flood when report about ignore transactions */
        bool report_ignore_transactions;

        using CachedBlock = std::pair<csdb::Pool, PublicKey>;
        std::vector<CachedBlock> future_blocks;

        /**
         * @brief   The last block sender.  
         *          It is guaranteed that upon return Result::Finish from onBlock() it contains the real
         *          sender of last stored block, value may differ from argument of onBlock(..., sender)
         *          method when we use cached blocks.
         */

        PublicKey last_block_sender;

        void try_blocks_in_cache(SolverContext& context, uint64_t last_seq);
    };

} // slv2
