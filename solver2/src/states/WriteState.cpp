#include "WriteState.h"
#include "../SolverCore.h"
#include <iostream>

namespace slv2
{
    void WriteState::beforeOn(SolverContext& /*context*/)
    {
        std::cout << name() << ": writing & sending block, then waiting for hashes (" << Consensus::MinTrustedNodes << " required)" << std::endl;
        cnt_hashes = 0;
    }

    Result WriteState::onRoundTable(SolverContext& /*context*/, const uint32_t round)
    {
        std::cout << name() << ": round table received: " << round << ". Who has sent this?!" << std::endl;
        return Result::Ignore;
    }

    Result WriteState::onHash(SolverContext& /*context*/, const Hash & /*hash*/, const PublicKey & /*sender*/)
    {
        std::cout << name() << ": hash received ";
        if(++cnt_hashes >= Consensus::MinTrustedNodes) {
            std::cout << "(enough), starting new round by broadcast new round table" << std::endl;
            return Result::Finish;
        }
        std::cout << "(" << Consensus::MinTrustedNodes - cnt_hashes << " still required)" << std::endl;
        return Result::Ignore;
    }

} // slv2
