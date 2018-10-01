#include "HandleBBState.h"
#include "../SolverContext.h"
#include <iostream>

namespace slv2
{

    void HandleBBState::on(SolverContext& /*context*/)
    {
        if(Consensus::Log) {
            std::cout << "do specific actions on BigBang" << std::endl;
        }
    }

}
