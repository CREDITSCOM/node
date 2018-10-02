#include "HandleBBState.h"
#include "../SolverContext.h"
#include <iostream>

namespace slv2
{

    void HandleBBState::on(SolverContext& context)
    {
        // if BigBang occur reduce hashes awaited to minimal value
        min_count_hashes = static_cast<int>(Consensus::MinTrustedNodes);
        int remains = min_count_hashes - static_cast<int>(context.cnt_hash_recv());
        if(Consensus::Log) {
            std::cout << name() << ": BigBang received, so resend last block and wait for hashes (" << remains << " more need)" << std::endl;
        }
        // only transition available is from WriteState, so repeat last block and go back there
        context.repeat_last_block();
        if(remains <= 0) {
            // enough hashes
            context.spawn_next_round();
        }
    }

}
