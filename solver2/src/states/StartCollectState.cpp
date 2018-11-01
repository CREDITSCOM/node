#include "StartCollectState.h"
#include "../SolverContext.h"
#include <lib/system/logger.hpp>

namespace slv2
{

    void StartCollectState::on(SolverContext & context)
    {
        auto cur_round = context.round();
        if(cur_round != 1) {
            if(Consensus::Log) {
                LOG_WARN(name() << ": this state is normally to be invoked on the first round only");
            }
        }
        if(Consensus::Log) {
            LOG_NOTICE(name() << ": schedule to send empty block to initiate consensus in " << Consensus::T_round << " ms");
        }
        SolverContext * pctx = &context;
        context.scheduler().InsertOnce(Consensus::T_round, [this, pctx]() {
            if(Consensus::Log) {
                LOG_NOTICE(name() << ": sending empty block");
            }
            WriteState::on(*pctx);

            if(Consensus::Log) {
                LOG_NOTICE(name() << ": schedule to repeat empty block consensus every " << Consensus::T_round << " ms");
            }
            tag_timeout = pctx->scheduler().InsertPeriodic(Consensus::T_round, [this, pctx]() {
                if(Consensus::Log) {
                    LOG_NOTICE(name() << ": sending empty block again");
                }
                pctx->repeat_last_block();
            });
        });
    }

    void StartCollectState::onRoundEnd(SolverContext & context)
    {
        cancel_timeout(context);

        WriteState::onRoundEnd(context);

        csdb::Pool pool {};
        auto sequence = context.round();
        if(Consensus::Log) {
            LOG_NOTICE(name() << ": sending transaction list #" << sequence << " of " << pool.transactions_count() << " items");
        }
        pool.set_sequence(sequence);
        context.send_transaction_list(pool);
        
    }

    void StartCollectState::cancel_timeout(SolverContext & context)
    {
        if(tag_timeout != CallsQueueScheduler::no_tag) {
            context.scheduler().Remove(tag_timeout);
            tag_timeout = CallsQueueScheduler::no_tag;
            if(Consensus::Log) {
                LOG_NOTICE(name() << ": cancel sending empty block to initiate consensus");
            }
        }
    }

} // slv2
