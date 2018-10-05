#include "TrustedState.h"
#include "../SolverContext.h"
#include "../SolverCompat.h"
#include "../Node.h"
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
                LOG_DEBUG(name() << ": duplicated vector received from " << (unsigned int) vect.Sender << ", ignore");
            }
            return Result::Ignore;
        }
        context.recv_vect_from(vect.Sender);
        context.generals().addvector(vect); // building matrix

        if(Consensus::Log) {
            LOG_NOTICE(name() << ": vector received from " << (unsigned int) vect.Sender << ",  total " << context.cnt_vect_recv());
        }
        if(test_vectors_completed(context))
        {
            if(Consensus::Log) {
                LOG_NOTICE(name() << ": enough vectors received");
            }

            //compose and send matrix!!!
            context.generals().addSenderToMatrix(context.own_conf_number());

            // context.generals().addmatrix(context.generals().getMatrix(), context.node().getConfidants()); is called from next:
            onMatrix(context, context.generals().getMatrix(), PublicKey {});

            context.node().sendMatrix(context.generals().getMatrix());
            return Result::Finish;

        }
        return Result::Ignore;
    }

    Result TrustedState::onMatrix(SolverContext& context, const Credits::HashMatrix & matr, const PublicKey & /*sender*/)
    {
        if(context.is_matr_recv_from(matr.Sender)) {
            if(Consensus::Log) {
                LOG_DEBUG(name() << ": duplicated matrix received from " << (unsigned int) matr.Sender << ", ignore");
            }
            return Result::Ignore;
        }
        context.recv_matr_from(matr.Sender);
        context.generals().addmatrix(matr, context.node().getConfidants());

        if(Consensus::Log) {
            LOG_NOTICE(name() << ": matrix received from " << (unsigned int) matr.Sender << ", total " << context.cnt_matr_recv());
        }

        if(test_matrices_completed(context)) {
            if(Consensus::Log) {
                LOG_NOTICE(name() << ": enough matrices received");
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
            LOG_NOTICE(name() << ": transaction list received, sending own vector back and processing it myself");
        }
        // the SolverCore updated own vector before call to us, so we can simply send it
        context.node().sendVector(context.hash_vector());
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
            Hash test_hash((char*) (context.node().getBlockChain().getLastWrittenHash().to_binary().data()));
            if(Consensus::Log) {
                LOG_NOTICE(name() << ": sending hash in reply to block sender");
            }
            context.node().sendHash(test_hash, sender);
        }
        return res;
    }

    bool TrustedState::test_vectors_completed(const SolverContext& context) const
    {
        return context.cnt_vect_recv() == context.node().getConfidants().size();
    }

    bool TrustedState::test_matrices_completed(const SolverContext& context) const
    {
        return context.cnt_matr_recv() == context.node().getConfidants().size();
    }

} // slv2
