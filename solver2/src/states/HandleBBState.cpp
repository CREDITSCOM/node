#include "HandleBBState.h"
#include "../SolverCore.h"
#include <iostream>

namespace slv2
{

    void HandleBBState::beforeOn(SolverContext& context)
    {
        std::cout << "do specific actions on BigBang" << std::endl;
        context.becomeNormal();
    }

}
