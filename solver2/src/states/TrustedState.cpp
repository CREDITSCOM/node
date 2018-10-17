#include "TrustedState.h"
#include "../SolverContext.h"
#include "../Consensus.h"
#include "../SolverCompat.h"
#include "../Generals.h"
#include <lib/system/logger.hpp>

namespace slv2
{
    void TrustedState::on(SolverContext& context)
    {
        DefaultStateBehavior::on(context);

        // its possible vectors or matrices already completed
        if(test_vectors_completed(context)) {
            // let context decide what to do
            if(Consensus::Log) {
                LOG_NOTICE(name() << ": enough vectors received");
            }
            context.vectors_completed();    
        }
        if(test_matrices_completed(context)) {
            // let context decide what to do
            if(Consensus::Log) {
                LOG_NOTICE(name() << ": enough matrices received");
            }
            context.matrices_completed();
        }
    }

    void TrustedState::off(SolverContext & context)
    {
        cancel_timeout_consensus(context);
        DefaultStateBehavior::off(context);
    }

    Result TrustedState::onRoundTable(SolverContext & context, const uint32_t round)
    {
        context.clear_vectors_cache();
        return DefaultStateBehavior::onRoundTable(context, round);
    }

    Result TrustedState::onVector(SolverContext& context, const Credits::HashVector & vect, const PublicKey & /*sender*/)
    {
        if(context.is_vect_recv_from(vect.Sender)) {
            if(Consensus::Log) {
                LOG_DEBUG(name() << ": duplicated vector [" << (unsigned int) vect.Sender << "] received, ignore");
            }
            return Result::Ignore;
        }
        context.recv_vect_from(vect.Sender);
        context.generals().addvector(vect); // building matrix

        if(Consensus::Log) {
            LOG_NOTICE(name() << ": vector [" << (unsigned int) vect.Sender << "] received,  total " << context.cnt_vect_recv());
        }
        if(test_vectors_completed(context))
        {
            if(Consensus::Log) {
                LOG_NOTICE(name() << ": vectors completed");
            }

            //compose and send matrix!!!
            context.generals().addSenderToMatrix((uint8_t) context.own_conf_number());

            // context.generals().addmatrix(context.generals().getMatrix(), context.node().getConfidants()); is called from next:
            onMatrix(context, context.generals().getMatrix(), PublicKey {});

            if(Consensus::Log) {
                LOG_NOTICE(name() << ": matrix [" << context.own_conf_number() << "] reply on completed vectors");
            }
            context.send_own_matrix();
            return Result::Finish;

        }
        start_timeout_consensus(context);
        return Result::Ignore;
    }

    Result TrustedState::onMatrix(SolverContext& context, const Credits::HashMatrix & matr, const PublicKey & /*sender*/)
    {
        if(context.is_matr_recv_from(matr.Sender)) {
            if(Consensus::Log) {
                LOG_DEBUG(name() << ": duplicated matrix [" << (unsigned int) matr.Sender << "] received, ignore");
            }
            return Result::Ignore;
        }
        context.recv_matr_from(matr.Sender);
        context.generals().addmatrix(matr, context.trusted());

        if(Consensus::Log) {
            LOG_NOTICE(name() << ": matrix [" << (unsigned int) matr.Sender << "] received, total " << context.cnt_matr_recv());
        }

        // update cached vectors from matrix
        uint8_t cnt_trusted = (uint8_t) context.cnt_trusted();
        for(uint8_t i = 0; i < cnt_trusted; ++i) {
            if(context.is_vect_recv_from(i)) {
                continue;
            }
            if(matr.hmatr[i].is_empty()) {
                continue;
            }
            context.cache_vector(matr.hmatr[i].Sender, matr.hmatr[i]);
        }

        if(test_matrices_completed(context)) {
            if(Consensus::Log) {
                LOG_NOTICE(name() << ": matrices completed");
            }
            return Result::Finish;
        }
        start_timeout_consensus(context);
        return Result::Ignore;
    }

    Result TrustedState::onTransactionList(SolverContext & context, const csdb::Pool & pool)
    {
        if(context.round() == 1) {
            if(pool.transactions_count() != 0) {
                if(Consensus::Log) {
                    LOG_ERROR(name() << ": transaction list on the 1st round must not contain transactions!");
                }
            }
        }
        if(Consensus::Log) {
            LOG_NOTICE(name() << ": vector [" << context.own_conf_number() << "] reply on transaction list");
        }
        // the SolverCore updated own vector before call to us, so we can simply send it
        context.send_own_vector();
        Result res = onVector(context, context.hash_vector(), PublicKey {});
        if(res == Result::Finish) {
            // let context immediately to decide what to do
            context.vectors_completed();
            // then to avoid undesired extra transition simply return Ignore
        }
        start_timeout_consensus(context);
        return Result::Ignore;
    }

    Result TrustedState::onBlock(SolverContext & context, csdb::Pool & block, const PublicKey & sender)
    {
        Result res = DefaultStateBehavior::onBlock(context, block, sender);
        if(res == Result::Finish) {
            DefaultStateBehavior::sendLastWrittenHash(context, sender);
        }
        return res;
    }

    bool TrustedState::test_vectors_completed(const SolverContext& context) const
    {
        return context.cnt_vect_recv() == context.cnt_trusted();
    }

    bool TrustedState::test_matrices_completed(const SolverContext& context) const
    {
        return context.cnt_matr_recv() == context.cnt_trusted();
    }

    bool TrustedState::try_restore_cached_vectors(SolverContext & context)
    {
        auto cnt = (uint8_t) context.cnt_trusted();
        for(uint8_t i = 0; i < cnt; ++i) {
            if(context.is_vect_recv_from(i)) {
                continue;
            }
            auto ptr = context.lookup_vector(i);
            if(ptr != nullptr) {
                // last arg actually ignored
                Result res = onVector(context, *ptr, PublicKey {});
                if(res == Result::Finish) {
                    // all vectors restored
                    return true;
                }
            }
        }
        return false;
    }

    void TrustedState::start_timeout_consensus(SolverContext & context)
    {
        if(Consensus::Log) {
            LOG_NOTICE(name() << ": start control timeout to wait vectors & matrices");
        }
        SolverContext * pctx = &context;
        tag_timeout_consensus = context.scheduler().InsertOnce(Consensus::T_consensus, [this, pctx]() {
            on_timeout_consensus(*pctx);
        });
    }

    void TrustedState::cancel_timeout_consensus(SolverContext & context)
    {
        if(tag_timeout_consensus != CallsQueueScheduler::no_tag) {
            if(Consensus::Log) {
                LOG_NOTICE(name() << ": cancel control timeout to wait vectors & matrices");
            }
            context.scheduler().Remove(tag_timeout_consensus);
            tag_timeout_consensus = CallsQueueScheduler::no_tag;
        }
    }

    void TrustedState::on_timeout_consensus(SolverContext & context)
    {
        if(!test_vectors_completed(context)) {
            if(context.cnt_vect_recv() < context.cnt_trusted()) {
                if(!try_restore_cached_vectors(context)) {
                    // actually cannot proceed to wait matrices
                    start_timeout_consensus(context);
                    return;
                }
            }
            // vectors completed after restore from cache, force to further state
            if(Consensus::Log) {
                LOG_WARN(name() << ": timeout to wait vectors has expired, force switch to further state");
            }
            context.vectors_completed();
            return;
        }
        if(!test_matrices_completed(context)) {
            if(context.cnt_matr_recv() < 1) {
                // cannot proceed to consensus
                start_timeout_consensus(context);
                return;
            }
            // force to complete matrices
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

