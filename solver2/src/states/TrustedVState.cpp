#include "TrustedVState.h"
#include "../SolverCore.h"
#include <iostream>

namespace slv2
{
    void TrustedVState::beforeOn(SolverContext& /*context*/)
    {
        cnt_matrices = 0;
    }

    Result TrustedVState::onRoundTable(SolverContext& /*context*/, const uint32_t round)
    {
        std::cout << name() << ": round table received: " << round << std::endl;
        return Result::Finish;
    }

    Result TrustedVState::onMatrix(SolverContext& /*context*/, const Credits::HashMatrix & /*matr*/, const PublicKey & /*sender*/)
    {
        std::cout << name() << ": matrix received" << std::endl;
        ++cnt_matrices;
        if(cnt_matrices >= Consensus::MinTrustedNodes) {
            return Result::Finish;
        }
        return Result::Ignore;
    }

} // slv2
