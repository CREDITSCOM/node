#include "TrustedState.h"
#include "../SolverCore.h"
#include <iostream>

namespace slv2
{
    void TrustedState::on(SolverContext& /*context*/)
    {
        cnt_matrices = 0;
        cnt_vectors = 0;
    }

    Result TrustedState::onRoundTable(SolverContext& /*context*/, const uint32_t round)
    {
        std::cout << name() << ": round table received: " << round << std::endl;
        return Result::Finish;
    }

    Result TrustedState::onVector(SolverContext& /*context*/, const Credits::HashVector & /*vect*/, const PublicKey & /*sender*/)
    {
        std::cout << name() << ": vector received" << std::endl;
        ++cnt_vectors;
        if(cnt_vectors >= Consensus::MinTrustedNodes) {
            return Result::Finish;
        }
        return Result::Ignore;
    }

    Result TrustedState::onMatrix(SolverContext& /*context*/, const Credits::HashMatrix & /*matr*/, const PublicKey & /*sender*/)
    {
        std::cout << name() << ": matrix received" << std::endl;
        ++cnt_matrices;
        if(cnt_matrices >= Consensus::MinTrustedNodes) {
            return Result::Finish;
        }
        return Result::Ignore;
    }

} // slv2
