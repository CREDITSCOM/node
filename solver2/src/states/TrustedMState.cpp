#include "TrustedMState.h"
#include "../SolverCore.h"
#include <iostream>

namespace slv2
{
    void TrustedMState::beforeOn(SolverContext& /*context*/)
    {
        cnt_vectors = 0;
    }

    Result TrustedMState::onRoundTable(SolverContext& /*context*/, const uint32_t round)
    {
        std::cout << name() << ": round table received: " << round << std::endl;
        return Result::Finish;
    }

    Result TrustedMState::onVector(SolverContext& /*context*/, const Credits::HashVector & /*vect*/, const PublicKey & /*sender*/)
    {
        std::cout << name() << ": vector received" << std::endl;
        ++cnt_vectors;
        if(cnt_vectors >= Consensus::MinTrustedNodes) {
            return Result::Finish;
        }
        return Result::Ignore;
    }

} // slv2
