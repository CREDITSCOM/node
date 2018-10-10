#include "StartNormalState.h"
#include "../SolverContext.h"
#include  "../Node.h"

namespace slv2
{

    Result slv2::StartNormalState::onBlock(SolverContext & context, csdb::Pool & block, const PublicKey & sender)
    {
        Result res = DefaultStateBehavior::onBlock(context, block, sender);
        if(res == Result::Finish) {
            DefaultStateBehavior::sendLastWrittenHash(context, sender);
        }
        return res;
    }

}
