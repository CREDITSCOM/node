#include "StartNormalState.h"
#include "../SolverContext.h"
#include  "../Node.h"

namespace slv2
{

    Result slv2::StartNormalState::onBlock(SolverContext & context, csdb::Pool & block, const PublicKey & sender)
    {
        Result res = DefaultStateBehavior::onBlock(context, block, sender);
        if(res == Result::Finish) {
            Hash test_hash((char*) (context.blockchain().getLastWrittenHash().to_binary().data()));
            if(Consensus::Log) {
                LOG_NOTICE(name() << ": sending hash in reply to block sender");
            }
            context.node().sendHash(test_hash, sender);
        }
        return res;
    }

}
