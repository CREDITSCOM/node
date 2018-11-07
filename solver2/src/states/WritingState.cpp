#include <WritingState.h>
#include <SolverContext.h>
#include <Consensus.h>

namespace slv2
{

    void WritingState::on(SolverContext & context)
    {
        //TODO:: remove call to context.spawn_next_round() from TrustedStage3State lines 57-61 before this state switch on !!!
        if(Consensus::Log) {
            LOG_EVENT(name() << ": spawn next round");
            context.spawn_next_round();
        }

    }

}
