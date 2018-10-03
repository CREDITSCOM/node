#include "HandleBBState.h"
#include "../SolverContext.h"
#include <lib/system/logger.hpp>

namespace slv2
{

    void HandleBBState::on(SolverContext& context)
    {
        // not WriteState::on(context) !!!
        DefaultStateBehavior::on(context);

        // if BigBang occur reduce hashes awaited to minimal value
        min_count_hashes = static_cast<int>(Consensus::MinTrustedNodes);
        int remains = min_count_hashes - static_cast<int>(context.cnt_hash_recv());
        if(Consensus::Log) {
            LOG_WARN(name() << ": BB received, so resend last block and wait for hashes (" << remains << " more need)");
        }
        // only transition available is from WriteState, so repeat last block and go back there
        context.repeat_last_block();
        if(remains <= 0) {
            // enough hashes
            context.spawn_next_round();
        }
    }

}
