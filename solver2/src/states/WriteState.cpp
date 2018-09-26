#include "WriteState.h"
#include "../SolverCore.h"
#include <iostream>

namespace slv2
{
    void WriteState::stateOn(SolverContext& /*context*/)
    {
        std::cout << getName() << ": writing & sending block, then waiting for hashes (" << Consensus::MinTrustedNodes << " required)" << std::endl;
        m_cntHashes = 0;
    }

    Result WriteState::onRoundTable(SolverContext& /*context*/, const uint32_t round)
    {
        std::cout << getName() << ": round table received: " << round << ". Who has sent this?!" << std::endl;
        return Result::Ignore;
    }

    Result WriteState::onHash(SolverContext& /*context*/, const Hash & /*hash*/, const PublicKey & /*sender*/)
    {
        std::cout << getName() << ": hash received ";
        if(++m_cntHashes >= Consensus::MinTrustedNodes) {
            std::cout << "(enough), starting new round by broadcast new round table" << std::endl;
            return Result::Finish;
        }
        std::cout << "(" << Consensus::MinTrustedNodes - m_cntHashes << " still required)" << std::endl;
        return Result::Ignore;
    }

} // slv2
