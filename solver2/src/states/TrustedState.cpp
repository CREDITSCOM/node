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
        // its possible vectors or matrices already completed
        if(test_vectors_completed(context)) {
            // let context decide what to do
            context.vectors_completed();
        }
        if(test_matrices_completed(context)) {
            // let context decide what to do
            context.matrices_completed();
        }
    }

    Result TrustedState::onRoundTable(SolverContext & context, const uint32_t round)
    {
        is_trans_list_recv = false;
        return DefaultStateBehavior::onRoundTable(context, round);
    }

    Result TrustedState::onVector(SolverContext& context, const Credits::HashVector & vect, const PublicKey & /*sender*/)
    {
        if(context.is_vect_recv_from(vect.Sender)) {
            //std::cout << "SOLVER> I've already got the vector from this Node" << std::endl;
            return Result::Ignore;
        }
        context.recv_vect_from(vect.Sender);
        context.generals().addvector(vect); // building matrix

        if(test_vectors_completed(context))
        {
            //compose and send matrix!!!
            context.generals().addSenderToMatrix(context.own_conf_number());

            // context.generals().addmatrix(context.generals().getMatrix(), context.node().getConfidants()); is called from next:
            onMatrix(context, context.generals().getMatrix(), PublicKey {});

            context.node().sendMatrix(context.generals().getMatrix());
            return Result::Finish;

        }
        std::cout << name() << ": vector received" << std::endl;
        return Result::Ignore;
    }

    Result TrustedState::onMatrix(SolverContext& context, const Credits::HashMatrix & matr, const PublicKey & /*sender*/)
    {
        if(context.is_matr_recv_from(matr.Sender)) {
            //std::cout << "SOLVER> I've already got the matrix from this Node" << std::endl;
            return Result::Ignore;
        }
        context.recv_matr_from(matr.Sender);
        context.generals().addmatrix(matr, context.node().getConfidants());

        if(test_matrices_completed(context)) {
            return Result::Finish;
        }
        return Result::Ignore;
    }

    Result TrustedState::onTransactionList(SolverContext & context, const csdb::Pool & /*pool*/)
    {
        // block duplicated lists
        if(is_trans_list_recv) {
            return Result::Ignore;
        }
        is_trans_list_recv = true;

        context.node().sendVector(context.hash_vector());

        Result res = onVector(context, context.hash_vector(), PublicKey {});
        if(res == Result::Finish) {
            // let context immediately to decide what to do
            context.vectors_completed();
            // then to avoid undesired extra transition simply return Ignore
        }
        return Result::Ignore;
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
