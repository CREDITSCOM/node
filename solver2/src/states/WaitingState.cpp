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
        writing_queue_num = (int)(((uint8_t)ptr->sender + (uint8_t)context.cnt_trusted() - (uint8_t)ptr->writer)) % (int)context.cnt_trusted();
        std::ostringstream os;
        os << prefix << "-" << (size_t) writing_queue_num;
        my_name = os.str();

        cslog() << name() << ": Sender = " << (int)ptr->sender << ", TrustedAmount = " << (int)context.cnt_trusted() << ", Writer = "<< (int)ptr->writer;
        if (writing_queue_num == 0) {
            cslog() << name() << ": becoming WRITER";
            context.request_role(Role::Writer);
            return;
        }


        //TODO: value = (Consensus::PostConsensusTimeout * "own number in "writing" queue")
        uint32_t value = 6000 * writing_queue_num;

        if(Consensus::Log) {
            LOG_EVENT(name() << ": start wait " << value / 1000 << " sec until new round");
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
        const auto ptr = context.stage3((uint8_t) context.own_conf_number());
        if(ptr == nullptr) {
            cserror() << name() << ": cannot access own stage data, didnt you forget to cancel this call?";
            return;
        }
        context.request_round_info(
            (uint8_t) ((int) (ptr->writer + writing_queue_num - 1)) % (int) context.cnt_trusted(), // previous "writer"
            (uint8_t) ((int) (ptr->writer + writing_queue_num + 1)) % (int) context.cnt_trusted()); // next "writer"
    }

}
