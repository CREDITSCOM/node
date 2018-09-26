#include "TrustedMState.h"
#include "../SolverCore.h"
#include <iostream>

namespace slv2
{
    void TrustedMState::stateOn(SolverContext& /*context*/)
    {
        m_cntVectors = 0;
    }

    Result TrustedMState::onRoundTable(SolverContext& /*context*/, const uint32_t round)
    {
        std::cout << getName() << ": round table received: " << round << std::endl;
        return Result::Finish;
    }

    Result TrustedMState::onVector(SolverContext& /*context*/, const Credits::HashVector & /*vect*/, const PublicKey & /*sender*/)
    {
        std::cout << getName() << ": vector received" << std::endl;
        ++m_cntVectors;
        if(m_cntVectors >= Consensus::MinTrustedNodes) {
            return Result::Finish;
        }
        return Result::Ignore;
    }

} // slv2
