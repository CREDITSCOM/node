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

    Result TrustedState::onRoundTable(SolverContext & context, const uint32_t round)
    {
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

        if(test_matrices_completed(context)) {
            if(Consensus::Log) {
                LOG_NOTICE(name() << ": matrices completed");
            }
            return Result::Finish;
        }
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

} // slv2
