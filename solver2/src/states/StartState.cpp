#include "StartState.h"
#include "../SolverCore.h"
#include <iostream>

namespace slv2
{

    Result StartState::onRoundTable(SolverContext& /*context*/, const uint32_t round)
    {
        std::cout << name() << ": round table received: " << round << std::endl;
        return Result::Finish;
    }

} // slv2
