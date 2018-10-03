#include "WriteState.h"
#include "../SolverContext.h"
#include "../Node.h"
#include "Consensus.h"
#include <lib/system/logger.hpp>

namespace slv2
{
    void WriteState::on(SolverContext& context)
    {
        DefaultStateBehavior::on(context);

        // No one other state must not store hashes this round!
        if(context.cnt_hash_recv() != 0) {
            if(Consensus::Log) {
                LOG_ERROR(name() << ": hashes must not be stored until I send a new block (" << context.cnt_hash_recv() << " are)");
            }
        }

        // adjust minimal hashes to await
        int tmp = static_cast<int>(context.cnt_trusted());
        if(tmp > min_count_hashes) {
            min_count_hashes = tmp;
        }

        context.node().becomeWriter();

        if(Consensus::Log) {
            LOG_NOTICE(name() << ": writing & sending block, then waiting for hashes (" << context.cnt_trusted() << ")");
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
                    LOG_NOTICE(name() << ": hash received (" << context.cnt_hash_recv() << "), " << not_enough << " more need");
                }
            }
            else {
                if(Consensus::Log) {
                    LOG_WARN(name() << ": hash received do not match!!!");
                }
            }
        }
        if(not_enough <= 0) {
            // received enough hashes
            if(Consensus::Log) {
                LOG_NOTICE(name() << ": request new round");
            }
            context.spawn_next_round();
            return Result::Finish;
        }
        return Result::Ignore;
    }

} // slv2
