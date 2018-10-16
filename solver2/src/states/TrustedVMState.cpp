#include "TrustedVMState.h"
#include "../SolverContext.h"
#include "../Generals.h"
#include "../Consensus.h"
#include "../Blockchain.h"
#include <lib/system/logger.hpp>

namespace slv2
{
    void TrustedVMState::on(SolverContext& context)
    {
        if(decide_to_write(context)) {
            if(Consensus::Log) {
                LOG_NOTICE(name() << ": request to become writing node");
            }
            // let context switch state to write
            context.request_role(Role::Write);
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
            context.trusted(),
            (uint8_t) context.own_conf_number(),
            context.blockchain().getHashBySequence(context.round() - 1)
        );
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
            return (wTrusted == (uint8_t) context.own_conf_number());
        }
        return false;
    }

} // slv2
