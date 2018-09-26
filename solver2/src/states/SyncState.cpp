#include "SyncState.h"
#include "../SolverCore.h"
#include <iostream>

namespace slv2
{

    Result SyncState::onRoundTable(SolverContext& /*context*/, const uint32_t round)
    {
        std::cout << getName() << ": round table received: " << round << std::endl;
        return Result::Finish;
    }

} // slv2
