#include "TrustedVMState.h"
#include "../SolverCore.h"
#include <iostream>

namespace slv2
{
    void TrustedVMState::on(SolverContext& context)
    {
        //TODO: make a decision to become writer or not
        if(++activation_counter % 2 == 0) {
            context.becomeWriter();
        }
    }

    Result TrustedVMState::onRoundTable(SolverContext& /*context*/, const uint32_t round)
    {
        std::cout << name() << ": round table received: " << round << std::endl;
        return Result::Finish;
    }
} // slv2
