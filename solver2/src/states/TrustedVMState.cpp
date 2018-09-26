#include "TrustedVMState.h"
#include "../SolverCore.h"
#include <iostream>

namespace slv2
{
    void TrustedVMState::stateOn(SolverContext& context)
    {
        //TODO: make a decision to become writer or not
        if(++m_activation_counter % 2 == 0) {
            context.becomeWriter();
        }
    }

    Result TrustedVMState::onRoundTable(SolverContext& /*context*/, const uint32_t round)
    {
        std::cout << getName() << ": round table received: " << round << std::endl;
        return Result::Finish;
    }
} // slv2
