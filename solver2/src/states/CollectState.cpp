#include "CollectState.h"
#include "../SolverContext.h"
#include "../Consensus.h"
#include "../Node.h"
#include <lib/system/logger.hpp>

namespace slv2
{
    void CollectState::on(SolverContext& context)
    {
        DefaultStateBehavior::on(context);

        if(Consensus::Log) {
            LOG_NOTICE(name() << ": starting to collect transactions of round #" << context.round());
        }
    }

    void CollectState::onRoundEnd(SolverContext & context)
    {
        do_send_tl(context, context.round());
    }

    Result CollectState::onTransaction(SolverContext& /*context*/, const csdb::Transaction & tr)
    {
        constexpr size_t logging_counter = 20;
        if(tr.is_valid()) {
            ++cnt_transactions;

            pool.add_transaction(tr);
            if(Consensus::Log && ((cnt_transactions % logging_counter) == 0)) {
                LOG_DEBUG(name() << ": transaction accepted (1) x" << logging_counter);
            }
        }
        else {
            if(Consensus::Log) {
                LOG_WARN(name() << ": invalid transaction received");
            }
        }
        return Result::Ignore;
    }

    void CollectState::do_send_tl(SolverContext& context, uint64_t sequence)
    {
        if(Consensus::Log) {
            LOG_NOTICE(name() << ": sending transaction list #" <<  sequence << " of " << pool.transactions_count() << " items");
        }
        pool.set_sequence(sequence);
        context.node().sendTransactionList(pool);
        pool = csdb::Pool {};
    }

} // slv2
