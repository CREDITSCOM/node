#include "TrustedVMState.h"
#include "../SolverContext.h"
#include "../Node.h"
#include "../Generals.h"
#include "../Consensus.h"
#include <lib/system/logger.hpp>

namespace slv2
{
    void TrustedVMState::on(SolverContext& context)
    {
        if(decide_to_write(context)) {
            if(Consensus::Log) {
                LOG_NOTICE(name() << ": request to become writing node");
            }
            // let context switch state
            context.become_writer();
        }
        // continue work as trusted node
    }

    Result TrustedVMState::onVector(SolverContext & context, const cs::HashVector & vect, const cs::PublicKey & sender)
    {
        // continue work as trusted but suppress further events on receive vectors
        TrustedState::onVector(context, vect, sender);
        return Result::Ignore;
    }

    Result TrustedVMState::onMatrix(SolverContext & context, const cs::HashMatrix & matr, const cs::PublicKey & sender)
    {
        // continue work as trusted but suppress further events on receive matrices
        TrustedState::onMatrix(context, matr, sender);
        return Result::Ignore;
    }

    bool TrustedVMState::decide_to_write(SolverContext& context)
    {
        uint8_t wTrusted = 4;/*context.generals().take_decision(
            context.node().getConfidants(),
            context.node().getMyConfNumber(),
            context.blockchain().getHashBySequence(context.node().getRoundNumber() - 1)
        );*/ // vshilkin
        if(Consensus::GeneralNotSelected == wTrusted) {
            if(Consensus::Log) {
                LOG_WARN(name() << ": consensus has not been reached");
            }
            //TODO: scheduleWriteNewBlock(T_coll_trans);
        }
        else {
            if(Consensus::Log) {
                LOG_NOTICE(name() << ": consensus has been reached");
            }
            return (wTrusted == context.own_conf_number());
        }
        return false;
    }

} // slv2
