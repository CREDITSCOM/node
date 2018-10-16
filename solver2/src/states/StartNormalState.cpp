#include "StartNormalState.h"
#include "../SolverContext.h"
#include "../Consensus.h"
#include  "../Node.h"
#include <lib/system/logger.hpp>

namespace slv2
{
    void StartNormalState::on(SolverContext& context)
    {
        // the problem: both in the first round and in any other we get round() == 1
        // the only difference, in the last case nobody send us any data including new round tables
        
        //TODO: experimental
        constexpr static uint32_t T_round_timeout = 7000;
        if(Consensus::Log) {
            LOG_WARN(name() << ": there is a round #" << context.round() << ", set timeout tracking for " << T_round_timeout << " msec");
        }
        SolverContext* pctx = &context;
        uint32_t rn = context.round();
        tag_timeout = context.scheduler().InsertOnce(T_round_timeout, [this, pctx, rn]() {
            if(Consensus::Log) {
                LOG_WARN(name() << ": round #" << rn << " is expired, request round table");
            }
            pctx->node().sendRoundTableRequest(rn);
        });
    }

    void StartNormalState::onRoundEnd(SolverContext& context)
    {
        cancel_timeout(context);
    }

    Result slv2::StartNormalState::onBlock(SolverContext& context, csdb::Pool & block, const cs::PublicKey & sender)
    {
        cancel_timeout(context);
        Result res = DefaultStateBehavior::onBlock(context, block, sender);
        if(res == Result::Finish) {
            DefaultStateBehavior::sendLastWrittenHash(context, sender);
        }
        return res;
    }

    void StartNormalState::cancel_timeout(SolverContext& context)
    {
        if(tag_timeout != CallsQueueScheduler::no_tag) {
            context.scheduler().Remove(tag_timeout);
            tag_timeout = CallsQueueScheduler::no_tag;
        }
    }

}
