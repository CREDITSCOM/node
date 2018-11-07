#include <states/PrimitiveWriteState.h>
#include <SolverContext.h>
#include <Consensus.h>
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
                trusted_candidates.assign(Consensus::MinTrustedNodes, (const char*)pctx->public_key().data());
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
                    trusted_candidates.push_back((const char *)pctx->public_key().data());
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

    Result PrimitiveWriteState::onHash(SolverContext & /*context*/, const Hash & /*hash*/, const PublicKey & sender)
    {
        // form "trusted candidates"
        trusted_candidates.push_back(sender);
        return Result::Ignore;
    }

    Result PrimitiveWriteState::onTransactionList(SolverContext & context, csdb::Pool & pool)
    {
        DefaultStateBehavior::onTransactionList(context, pool);
        csdb::Pool accepted {};
        if(pool.transactions_count() > 0) {
            for(const auto& t : pool.transactions()) {
                accepted.add_transaction(t);
            }
        }
        context.accept_transactions(accepted);
        return Result::Ignore;
    }
} // slv2
