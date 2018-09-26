#include "NormalState.h"
#include "../SolverCore.h"
#include <iostream>

namespace slv2
{

    Result NormalState::onRoundTable(SolverContext& /*context*/, const uint32_t round)
    {
        std::cout << getName() << ": round table received: " << round << std::endl;
        return Result::Finish;
    }

} // slv2
