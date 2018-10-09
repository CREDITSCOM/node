#include "StartCollectState.h"
#include "../SolverContext.h"
#include "../Node.h"

namespace slv2
{

    void StartCollectState::on(SolverContext & context)
    {
        CollectState::on(context);

        auto cur_round = context.round();
        if(cur_round != 1) {
            if(Consensus::Log) {
                LOG_WARN(name() << ": this state is normally to be invoked on the first round only");
            }
        }
        if(Consensus::Log) {
            LOG_NOTICE(name() << ": schedule to send empty transaction list to initiate consensus every " << Consensus::T_round << " ms");
        }
        SolverContext * pctx = &context;
        tag_timeout = context.scheduler().InsertPeriodic(Consensus::T_round, [this, pctx]() {
            if(Consensus::Log) {
                LOG_NOTICE(name() << ": sending empty transaction list #0 of 0 items");
            }
            csdb::Pool p {};
            p.set_sequence(0);
            pctx->node().sendTransactionList(p);
        });
    }

    void StartCollectState::onRoundEnd(SolverContext & context)
    {
        cancel_timeout(context);
        CollectState::onRoundEnd(context);
    }

    Result StartCollectState::onBlock(SolverContext & context, csdb::Pool & block, const PublicKey & sender)
    {
        cancel_timeout(context);
        return CollectState::onBlock(context, block, sender);
    }

    void StartCollectState::cancel_timeout(SolverContext & context)
    {
        if(tag_timeout != CallsQueueScheduler::no_tag) {
            context.scheduler().Remove(tag_timeout);
            tag_timeout = CallsQueueScheduler::no_tag;
            if(Consensus::Log) {
                LOG_NOTICE(name() << ": cancel sending empty transaction list to initiate consensus");
            }
        }
    }

} // slv2
