#include "SolverContext.h"
#include "SolverCore.h"
#include "Node.h"

namespace slv2
{

    uint8_t SolverContext::own_conf_number() const
    {
        return core.pnode->getMyConfNumber();
    }

    size_t SolverContext::cnt_trusted() const
    {
        return core.pnode->getConfidants().size();
    }

    void SolverContext::spawn_next_round()
    {
        core.pnode->initNextRound(core.pnode->getMyPublicKey(), std::move(core.recv_hash));
    }

} // slv2
