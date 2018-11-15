#include "WaitingState.h"
#include <Consensus.h>
#include <SolverContext.h>
#include <sstream>

namespace slv2
{

    void WaitingState::on(SolverContext & context)
    {
        //TODO:: calculate my queue number starting from writing node:
        const auto ptr = context.stage3((uint8_t)context.own_conf_number());
        writing_queue_num = (int)(((uint8_t)ptr->sender + (uint8_t)context.cnt_trusted() - (uint8_t)ptr->writer) % (int)context.cnt_trusted());
        LOG_EVENT(name() << ": Sender = " << (int)ptr->sender << ", TrustedAmount = " << (int)context.cnt_trusted() << ", Writer = "<< (int)ptr->writer );
        LOG_EVENT(name() << ": before becoming WRITER!!!! ");
        if (writing_queue_num == 0) {
            LOG_EVENT(name() << ": becoming WRITER!!!! ");
            context.request_role(Role::Writer);
            return;
        }

        std::ostringstream os;
        os << prefix << "-" << (size_t) writing_queue_num;
        my_name = os.str();

        //TODO: value = (Consensus::PostConsensusTimeout * "own number in "writing" queue")
        uint32_t value = 60000;

        if(Consensus::Log) {
            LOG_EVENT(name() << ": start wait " << value / 60 << " sec until new round");
        }
        SolverContext *pctx = &context;
        timeout_round.start(
            context.scheduler(), value, [pctx, this]() {
            if(Consensus::Log) {
                cslog() << name() << ": time to wait new round is expired";
            }   
            activate_new_round(*pctx);
        }, true/*replace existing*/);
    }

    void WaitingState::off(SolverContext & /*context*/)
    {
        if(timeout_round.cancel()) {
            if(Consensus::Log) {
                cslog() << name() << ": cancel wait new round";
            }
        }
    }

    void WaitingState::activate_new_round(SolverContext & context)
    {
      cslog() << name() << ": activating new round ";
      context.request_round_info(context.stage3((uint8_t)context.own_conf_number())->writer);
    }

}
