#include "TrustedVMState.h"
#include "../SolverContext.h"
#include "../Node.h"
#include "../Generals.h"
#include "Consensus.h"

#include <iostream>

namespace slv2
{
    void TrustedVMState::on(SolverContext& context)
    {
        if(decide_to_write(context)) {
            // let context switch state
            context.become_writer();
        }
        // continue work as trusted node
    }

    Result TrustedVMState::onVector(SolverContext & context, const Credits::HashVector & vect, const PublicKey & sender)
    {
        // continue work as trusted but suppress further events on receive vectors
        TrustedState::onVector(context, vect, sender);
        return Result::Ignore;
    }

    Result TrustedVMState::onMatrix(SolverContext & context, const Credits::HashMatrix & matr, const PublicKey & sender)
    {
        // continue work as trusted but suppress further events on receive matrices
        TrustedState::onMatrix(context, matr, sender);
        return Result::Ignore;
    }

    bool TrustedVMState::decide_to_write(SolverContext& context)
    {
        uint8_t wTrusted = context.generals().take_decision(
            context.node().getConfidants(),
            context.node().getMyConfNumber(),
            context.node().getBlockChain().getHashBySequence(context.node().getRoundNumber() - 1)
        );
        if(Consensus::GeneralNotSelected == wTrusted) {
            // std::cout << "SOLVER> CONSENSUS WASN'T ACHIEVED!!!" << std::endl;
            //TODO: scheduleWriteNewBlock(T_coll_trans);
        }
        else {
            // std::cout << "SOLVER> wTrusted = " << (int)wTrusted << std::endl;
            return (wTrusted == context.own_conf_number());
        }
        return false;
    }

} // slv2
