#include "WriteState.h"
#include "../SolverCore.h"
#include "../Node.h"
#include "Consensus.h"

#include <iostream>

namespace slv2
{
    void WriteState::on(SolverContext& context)
    {
        // No one other state must not store hashes this round!
        assert(context.cnt_hash_recv() == 0);

        context.node().becomeWriter();

        std::cout << name() << ": writing & sending block, then waiting for hashes (" << context.cnt_trusted() << ")" << std::endl;
        context.makeAndSendBlock();
        pown = std::make_unique<Hash>((char*) (context.node().getBlockChain().getLastWrittenHash().to_binary().data()));
    }

    Result WriteState::onRoundTable(SolverContext& /*context*/, const uint32_t round)
    {
        std::cout << name() << ": round table received: " << round << ". Wtf has sent this?!" << std::endl;
        return Result::Ignore;
    }

    Result WriteState::onHash(SolverContext& context, const Hash& hash, const PublicKey& sender)
    {
        auto not_enough = static_cast<int>(Consensus::MinTrustedNodes) - static_cast<int>(context.cnt_hash_recv());
        if(not_enough > 0) {
            if(hash == *pown) {
                context.recv_hash_from(sender);
                not_enough--;
            }
            else {
                std::cout << name() << ": hashes do not match!!!" << std::endl;
            }
        }
        if(not_enough <= 0) {
            // received enough hashes
            context.spawn_next_round();
            return Result::Finish;
        }
        return Result::Ignore;
    }

} // slv2
