#include "CollectState.h"
#include "../SolverContext.h"
#include "../Node.h"
#include <lib/system/logger.hpp>

namespace slv2
{
    void CollectState::on(SolverContext& context)
    {
        DefaultStateBehavior::on(context);

        auto cur_round = context.round();
        if(cur_round == 1) {
            if(Consensus::Log) {
                LOG_NOTICE(name() << ": at the 1st round schedule to send empty transaction list to initiate consensus after " << Consensus::T_round << " ms");
            }
            SolverContext * pctx = &context;
            context.scheduler().InsertOnce(Consensus::T_round, [this, pctx]() {
                if(Consensus::Log) {
                    LOG_NOTICE(name() << ": sending empty transaction list #0 of 0 items");
                }
                csdb::Pool p {};
                p.set_sequence(0);
                pctx->node().sendTransactionList(p);
            });
        }
        if(Consensus::Log) {
            LOG_NOTICE(name() << ": starting to collect transactions of round #" << context.round());
        }
    }

    void CollectState::onRoundEnd(SolverContext & context)
    {
        if(tag_timeout != CallsQueueScheduler::no_tag) {
            context.scheduler().Remove(tag_timeout);
            tag_timeout = CallsQueueScheduler::no_tag;
        }
        do_send_tl(context, context.round());
    }

    Result CollectState::onTransaction(SolverContext& context, const csdb::Transaction & tr)
    {
        constexpr size_t logging_counter = 20;
        if(tr.is_valid())
        {
            ++cnt_transactions;

            if(!context.is_spammer()) {
                if(context.verify(tr)) {
                    pool.add_transaction(tr);
                    if(Consensus::Log && ((cnt_transactions % logging_counter) == 0)) {
                        LOG_DEBUG(name() << ": transaction accepted (1) x" << logging_counter);
                    }
                }
                else {
                    if(Consensus::Log) {
                        LOG_WARN(name() << ": wrong transaction signature");
                    }
                }
            }
            else {
                pool.add_transaction(tr);
                // too much flood
                if(Consensus::Log && ((cnt_transactions % logging_counter) == 0)) {
                    LOG_DEBUG(name() << ": spammer transaction accepted (1) x" << logging_counter);
                }
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
