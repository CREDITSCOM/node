#include "WriteState.h"
#include "../SolverContext.h"
#include "../Node.h"
#include "../Consensus.h"
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
        int tmp = static_cast<int>(context.cnt_trusted_desired());
        if(tmp > min_count_hashes) {
            min_count_hashes = tmp;
        }

        context.node().becomeWriter();

        context.create_and_send_new_block();
        pown = std::make_unique<Hash>((char*) (context.blockchain().getLastWrittenHash().to_binary().data()));
        if(Consensus::Log) {
            constexpr const size_t hash_len = sizeof(pown->str) / sizeof(pown->str[0]);
            LOG_NOTICE(name() << ": writing & sending block, then waiting for hashes (" << context.cnt_trusted() << ") = " << byteStreamToHex(pown->str, hash_len));
        }

        // launch timeout control to reduce count of desired candidates on delay
        if(Consensus::Log) {
            LOG_NOTICE(name() << ": launch timeout control to test count of hashes every " << Consensus::T_hash << " ms");
        }
        SolverContext * pctx = &context;
        tag_timeout = context.scheduler().InsertPeriodic(Consensus::T_hash, [this,pctx]() {
            // time to wait for hashes is expired => spawn new round if hashes enough
            if(pctx->cnt_hash_recv() >= Consensus::MinTrustedNodes) {
                // we have got minimal required count of hashes, so we can spawn new round
                if(Consensus::Log) {
                    LOG_WARN(name() << ": we havent got all desired hashes but enough to request new round");
                }
                pctx->spawn_next_round();
            }
        });
    }

    void WriteState::onRoundEnd(SolverContext & context)
    {
        if(tag_timeout != CallsQueueScheduler::no_tag) {
            context.scheduler().Remove(tag_timeout);
            tag_timeout = CallsQueueScheduler::no_tag;
        }
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
                    constexpr const size_t hash_len = sizeof(sender.str) / sizeof(sender.str[0]);
                    LOG_WARN(name() << ": hash received do not match!!! Sender " << byteStreamToHex(sender.str, hash_len));
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
