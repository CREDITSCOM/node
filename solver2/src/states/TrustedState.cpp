#include "TrustedState.h"
#include "../SolverCore.h"
#include <iostream>

namespace slv2
{
    void TrustedState::stateOn(SolverContext& /*context*/)
    {
        m_cntMatrices = 0;
        m_cntVectors = 0;
    }

    Result TrustedState::onRoundTable(SolverContext& /*context*/, const uint32_t round)
    {
        std::cout << getName() << ": round table received: " << round << std::endl;
        return Result::Finish;
    }

    Result TrustedState::onVector(SolverContext& /*context*/, const Credits::HashVector & /*vect*/, const PublicKey & /*sender*/)
    {
        std::cout << getName() << ": vector received" << std::endl;
        ++m_cntVectors;
        if(m_cntVectors >= Consensus::MinTrustedNodes) {
            return Result::Finish;
        }
        return Result::Ignore;
    }

    Result TrustedState::onMatrix(SolverContext& /*context*/, const Credits::HashMatrix & /*matr*/, const PublicKey & /*sender*/)
    {
        std::cout << getName() << ": matrix received" << std::endl;
        ++m_cntMatrices;
        if(m_cntMatrices >= Consensus::MinTrustedNodes) {
            return Result::Finish;
        }
        return Result::Ignore;
    }

} // slv2
