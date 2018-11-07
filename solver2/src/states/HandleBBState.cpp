#include <states/HandleBBState.h>
#include <SolverContext.h>
#include <lib/system/logger.hpp>

namespace slv2
{

    void HandleBBState::on(SolverContext& context)
    {
        if(context.is_block_deferred()) {
            context.drop_deferred_block();
        }
        if(Consensus::Log) {
            LOG_WARN(name() << ": BigBang processing is implemented in Node class");
        }
        DefaultStateBehavior::on(context);
    }

}
