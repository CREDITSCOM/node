#include <states/primitivewritestate.hpp>
#include <solvercontext.hpp>
#include <consensus.hpp>
#include <lib/system/logger.hpp>

namespace slv2
{
    void PrimitiveWriteState::on(SolverContext& context)
    {
        DefaultStateBehavior::on(context);

        SolverContext * pctx = &context;

        if(context.round() == 0) {
            if(Consensus::Log) {
                LOG_NOTICE(name() << ": start track timeout " << Consensus::T_round << " ms to spawn first round");
            }
            context.scheduler().InsertOnce(Consensus::T_round, [pctx, this]() {
                if(Consensus::Log) {
                    LOG_NOTICE(name() << ": it is time to spawn first round");
                }
                trusted_candidates.assign(Consensus::MinTrustedNodes, pctx->public_key());
                    pctx->next_trusted_candidates(trusted_candidates);
                trusted_candidates.clear();
                pctx->spawn_first_round();
            }, true);
            return;
        }

        if(Consensus::Log) {
            LOG_NOTICE(name() << ": start track timeout " << Consensus::DefaultStateTimeout << " ms to complete round");
        }
        tag_timeout = context.scheduler().InsertPeriodic(Consensus::DefaultStateTimeout, [pctx, this]() {
            if(Consensus::Log) {
                LOG_NOTICE(name() << ": round duration is expired");
            }
            // "complete" trusted candidates with own key
            if(Consensus::MinTrustedNodes > trusted_candidates.size()) {
                size_t cnt = Consensus::MinTrustedNodes - trusted_candidates.size();
                for(size_t i = 0; i < cnt; i++) {
                    trusted_candidates.emplace_back(pctx->public_key());
                }
            }
            pctx->next_trusted_candidates(trusted_candidates);
            trusted_candidates.clear();
            pctx->spawn_next_round();
        });
    }

    void PrimitiveWriteState::off(SolverContext& context)
    {
        if(tag_timeout != CallsQueueScheduler::no_tag) {
            context.scheduler().Remove(tag_timeout);
            tag_timeout = CallsQueueScheduler::no_tag;
        }
        DefaultStateBehavior::off(context);
    }

    Result PrimitiveWriteState::onHash(SolverContext & /*context*/, const csdb::PoolHash & /*pool_hash*/, const cs::PublicKey & sender)
    {
        // form "trusted candidates"
        trusted_candidates.emplace_back(sender);
        return Result::Ignore;
    }

    Result PrimitiveWriteState::onSyncTransactions(SolverContext & context, cs::RoundNumber round)
    {
        DefaultStateBehavior::onSyncTransactions(context, round);
        return Result::Ignore;
    }
} // slv2
