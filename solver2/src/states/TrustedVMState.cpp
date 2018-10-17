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

    TrustedVMState::Decision TrustedVMState::parse_generals_decision(SolverContext& context, uint8_t decision)
    {
        if(Consensus::GeneralNotSelected == decision) {
            if(Consensus::Log) {
                LOG_WARN(name() << ": consensus has not been reached");
            }
            return Decision::NotTaken;
        }
        if(Consensus::Log) {
            LOG_NOTICE(name() << ": generals select [" << (size_t) decision << "] from " << context.cnt_matr_recv());
        }
        // counting actual matrices, normally (not always) it is equal to count of trusted
        uint8_t cnt_trusted = (uint8_t) context.cnt_trusted();
        uint8_t own_number = (uint8_t) context.own_conf_number();
        uint8_t i_matr = 0;
        for(uint8_t i = 0; i < cnt_trusted; ++i) {
            if(!context.is_matr_recv_from(i)){
                // skip this node
                continue;
            }
            if(i_matr == decision) {
                // i_matr counts only those has matrix
                if(Consensus::Log) {
                    LOG_NOTICE(name() << ": consensus has been reached in favor [" << (unsigned)i
                        << "] (decision index " << (unsigned int) decision << ")");
                }
                // i counts every trusted node
                if(i == own_number) {
                    return Decision::BecomeWrite;
                }
                break;
            }
            ++i_matr;
        }
        return Decision::StayTrusted;
    }

    bool TrustedVMState::decide_to_write(SolverContext& context)
    {
        if(test_vectors_completed(context) && test_matrices_completed(context)) {
            uint8_t trusted_idx = context.generals().take_decision(
                context.trusted(),
                (uint8_t) context.own_conf_number(),
                context.blockchain().getHashBySequence(context.round() - 1)
            );
            Decision result = parse_generals_decision(context, trusted_idx);
            if(result != Decision::NotTaken) {
                return (result == Decision::BecomeWrite);
            }
        }
        auto cnt_matr_recv = context.cnt_matr_recv();
        if(0 == cnt_matr_recv) {
            if(Consensus::Log) {
                LOG_WARN(name() << ": unable to start consensus, no matrices at all");
            }
            return false;
        }
        // consensus has not been reached or timeout has expired
        if(Consensus::Log) {
            LOG_WARN(name() << ": force consensus due timeout");
        }
        uint8_t trusted_idx = context.generals().takeUrgentDecision(
            cnt_matr_recv, // !!! actual trusted responses
            context.blockchain().getHashBySequence(context.round() - 1));
        return (Decision::BecomeWrite == parse_generals_decision(context, trusted_idx));
    }

} // slv2
