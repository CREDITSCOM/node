#include "TrustedState.h"
#include "../SolverContext.h"
#include "../Consensus.h"
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

    Result TrustedState::onVector(SolverContext& context, const cs::HashVector & vect, const cs::PublicKey & /*sender*/)
    {
        if(context.is_vect_recv_from(vect.sender)) {
            if(Consensus::Log) {
                LOG_DEBUG(name() << ": duplicated vector received from [" << (unsigned int) vect.sender << "], ignore");
            }
            return Result::Ignore;
        }
        context.recv_vect_from(vect.sender);
        context.generals().addVector(vect); // building matrix

        if(Consensus::Log) {
            LOG_NOTICE(name() << ": vector received from [" << (unsigned int) vect.sender << "],  total " << context.cnt_vect_recv());
        }
        if(test_vectors_completed(context))
        {
            if(Consensus::Log) {
                LOG_NOTICE(name() << ": enough vectors received");
            }

            //compose and send matrix!!!
            context.generals().addSenderToMatrix(context.own_conf_number());

            // context.generals().addMatrix(context.generals().getMatrix(), context.node().getConfidants()); is called from next:
            onMatrix(context, context.generals().getMatrix(), cs::PublicKey {});

            context.node().sendMatrix(context.generals().getMatrix());
            return Result::Finish;

        }
        return Result::Ignore;
    }

    Result TrustedState::onMatrix(SolverContext& context, const cs::HashMatrix & matr, const cs::PublicKey & /*sender*/)
    {
        if(context.is_matr_recv_from(matr.sender)) {
            if(Consensus::Log) {
                LOG_DEBUG(name() << ": duplicated matrix received from [" << (unsigned int) matr.sender << "], ignore");
            }
            return Result::Ignore;
        }
        context.recv_matr_from(matr.sender);
        //context.generals().addMatrix(matr, context.node().getConfidants()); vshilkin

        if(Consensus::Log) {
            LOG_NOTICE(name() << ": matrix received from [" << (unsigned int) matr.sender << "], total " << context.cnt_matr_recv());
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
            LOG_NOTICE(name() << ": reply on transaction list with own vector");
        }
        // the SolverCore updated own vector before call to us, so we can simply send it
        context.node().sendVector(context.hash_vector());
        Result res = onVector(context, context.hash_vector(), cs::PublicKey {});
        if(res == Result::Finish) {
            // let context immediately to decide what to do
            context.vectors_completed();
            // then to avoid undesired extra transition simply return Ignore
        }
        return Result::Ignore;
    }

    Result TrustedState::onBlock(SolverContext & context, csdb::Pool & block, const cs::PublicKey & sender)
    {
        Result res = DefaultStateBehavior::onBlock(context, block, sender);
        if(res == Result::Finish) {
            DefaultStateBehavior::sendLastWrittenHash(context, sender);
        }
        return res;
    }

    bool TrustedState::test_vectors_completed(const SolverContext& context) const
    {
      return true;
//        return context.cnt_vect_recv() == context.node().getConfidants().size(); // vshilkin
    }

    bool TrustedState::test_matrices_completed(const SolverContext& context) const
    {
      return true;
//        return context.cnt_matr_recv() == context.node().getConfidants().size(); // vshilkin
    }

} // slv2
