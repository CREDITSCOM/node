#include "TrustedState.h"
#include "../SolverContext.h"
#include "../SolverCompat.h"
#include "../Node.h"
#include "../Generals.h"

#include <iostream>

namespace slv2
{
    void TrustedState::on(SolverContext& context)
    {
        DefaultStateBehavior::on(context);

        // its possible vectors or matrices already completed
        if(test_vectors_completed(context)) {
            // let context decide what to do
            if(Consensus::Log) {
                std::cout << name() << ": enough vectors received" << std::endl;
            }
            context.vectors_completed();
        }
        if(test_matrices_completed(context)) {
            // let context decide what to do
            if(Consensus::Log) {
                std::cout << name() << ": enough matrices received" << std::endl;
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
                std::cout << name() << ": duplicated vector received from " << (unsigned int) vect.Sender << ", ignore" << std::endl;
            }
            return Result::Ignore;
        }
        context.recv_vect_from(vect.Sender);
        context.generals().addvector(vect); // building matrix

        if(Consensus::Log) {
            std::cout << name() << ": vector received from " << (unsigned int) vect.Sender << ",  total " << context.cnt_vect_recv() << std::endl;
        }
        if(test_vectors_completed(context))
        {
            if(Consensus::Log) {
                std::cout << name() << ": enough vectors received" << std::endl;
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
                std::cout << name() << ": duplicated matrix received from " << (unsigned int) matr.Sender << ", ignore" << std::endl;
            }
            return Result::Ignore;
        }
        context.recv_matr_from(matr.Sender);
        context.generals().addmatrix(matr, context.node().getConfidants());

        if(Consensus::Log) {
            std::cout << name() << ": matrix received from " << (unsigned int) matr.Sender << ", total " << context.cnt_matr_recv() << std::endl;
        }

        if(test_matrices_completed(context)) {
            if(Consensus::Log) {
                std::cout << name() << ": enough matrices received" << std::endl;
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
                    std::cout << name() << ": (warning) transaction list on the 1st round must not contain transactions!" << std::endl;
                }
            }
        }
        if(Consensus::Log) {
            std::cout << name() << ": transaction list received, sending own vector back and processing it myself" << std::endl;
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
                std::cout << name() << ": sending hash in reply to block sender" << std::endl;
            }
            // in case of some block was restored from inner cache the last_block_sender contains correct value, other then
            // argument sender value
            context.node().sendHash(test_hash, DefaultStateBehavior::last_block_sender);
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
