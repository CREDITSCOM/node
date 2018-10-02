#include "WriteState.h"
#include "../SolverContext.h"
#include "../Node.h"
#include "Consensus.h"

#include <iostream>

namespace slv2
{
    void WriteState::on(SolverContext& context)
    {
        DefaultStateBehavior::on(context);

        // No one other state must not store hashes this round!
        assert(context.cnt_hash_recv() == 0);

        // adjust minimal hashes to await
        int tmp = static_cast<int>(context.cnt_trusted());
        if(tmp > min_count_hashes) {
            min_count_hashes = tmp;
        }

        context.node().becomeWriter();

        if(Consensus::Log) {
            std::cout << name() << ": writing & sending block, then waiting for hashes (" << context.cnt_trusted() << ")" << std::endl;
        }
        context.store_and_send_block();
        pown = std::make_unique<Hash>((char*) (context.node().getBlockChain().getLastWrittenHash().to_binary().data()));
    }

    Result WriteState::onHash(SolverContext& context, const Hash& hash, const PublicKey& sender)
    {
        // can use Consensus::MinTrustedNodes if timeout occur
        auto not_enough = min_count_hashes - static_cast<int>(context.cnt_hash_recv());
        if(not_enough > 0) {
            if(hash == *pown) {
                context.recv_hash_from(sender);
                not_enough--;
                if(Consensus::Log) {
                    std::cout << name() << ": hash received (" << context.cnt_hash_recv() << "), " << not_enough << " more need" << std::endl;
                }
            }
            else {
                if(Consensus::Log) {
                    std::cout << name() << ": hash received do not match!!!" << std::endl;
                }
            }
        }
        if(not_enough <= 0) {
            // received enough hashes
            if(Consensus::Log) {
                std::cout << name() << ": request new round" << std::endl;
            }
            context.spawn_next_round();
            return Result::Finish;
        }
        return Result::Ignore;
    }

} // slv2
