#include "TrustedVState.h"
#include "../SolverContext.h"
#include "../Consensus.h"

namespace slv2
{
    void TrustedVState::on(SolverContext& context)
    {
        // makes initial tests:
        TrustedState::on(context);
        // start again timeout control (inherited from TrustedState)
        start_timeout_matrices(context);
    }

    void TrustedVState::off(SolverContext & context)
    {
        cancel_timeout_matrices(context);
        TrustedState::off(context);
    }

    Result TrustedVState::onVector(SolverContext & context, const Credits::HashVector & vect, const PublicKey & sender)
    {
        // continue work as trusted but suppress further events on receive vectors
        TrustedState::onVector(context, vect, sender);
        return Result::Ignore;
    }
    
    void TrustedVState::start_timeout_matrices(SolverContext & context)
    {
        if(Consensus::Log) {
            LOG_NOTICE(name() << ": start control timeout to wait matrices");
        }
        SolverContext * pctx = &context;
        tag_timeout_matrices = context.scheduler().InsertOnce(Consensus::T_consensus, [this, pctx]() {
            on_timeout_matrices(*pctx);
        });
    }

    void TrustedVState::cancel_timeout_matrices(SolverContext & context)
    {
        if(tag_timeout_matrices != CallsQueueScheduler::no_tag) {
            if(Consensus::Log) {
                LOG_NOTICE(name() << ": cancel control timeout to wait matrices");
            }
            context.scheduler().Remove(tag_timeout_matrices);
            tag_timeout_matrices = CallsQueueScheduler::no_tag;
        }
    }

    void TrustedVState::on_timeout_matrices(SolverContext & context)
    {
        if(! TrustedState::test_matrices_completed(context)) {
            if(context.cnt_matr_recv() < 1) {
                // impossible to force consensus without any matrix
                start_timeout_matrices(context);
                return;
            }
            //TODO: lookup vectors in matrices received if any
            // force to complete vectors
            if(Consensus::Log) {
                LOG_WARN(name() << ": timeout to wait matrices has expired, force switch to further state");
            }
            context.matrices_completed();
            return;
        }
        if(Consensus::Log) {
            LOG_ERROR(name() << ": timeout has expired while we have got all data required for consensus. What do we wait for?");
        }
    }

} // slv2
