#include "TrustedVState.h"
#include "../SolverCore.h"
#include <iostream>

namespace slv2
{
    void TrustedVState::stateOn(SolverContext& /*context*/)
    {
        m_cntMatrices = 0;
    }

    Result TrustedVState::onRoundTable(SolverContext& /*context*/, const uint32_t round)
    {
        std::cout << getName() << ": round table received: " << round << std::endl;
        return Result::Finish;
    }

    Result TrustedVState::onMatrix(SolverContext& /*context*/, const Credits::HashMatrix & /*matr*/, const PublicKey & /*sender*/)
    {
        std::cout << getName() << ": matrix received" << std::endl;
        ++m_cntMatrices;
        if(m_cntMatrices >= Consensus::MinTrustedNodes) {
            return Result::Finish;
        }
        return Result::Ignore;
    }

} // slv2
