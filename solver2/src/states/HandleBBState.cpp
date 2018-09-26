#include "HandleBBState.h"
#include "../SolverCore.h"
#include <iostream>

namespace slv2
{

    void HandleBBState::stateOn(SolverContext& context)
    {
        std::cout << "do specific actions on BigBang" << std::endl;
        context.becomeNormal();
    }

}
