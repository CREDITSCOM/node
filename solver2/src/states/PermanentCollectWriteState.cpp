#include "PermanentCollectWriteState.h"
#include "../SolverContext.h"
#include "../Node.h"
#include <lib/system/logger.hpp>

namespace slv2
{

    void PermanentCollectWriteState::on(SolverContext & context)
    {
        start_collect_transactions(context);
    }

    void PermanentCollectWriteState::onRoundEnd(SolverContext & context)
    {
        cancel_collect_hashes(context);
        cancel_collect_transactions(context);
    }

    Result PermanentCollectWriteState::onRoundTable(SolverContext & context, const uint32_t /*round*/)
    {
        // adjust minimal hashes to await       
        int tmp = static_cast<int>(context.cnt_trusted_desired());
        if(tmp > min_count_hashes) {
            min_count_hashes = tmp;
        }
        start_collect_transactions(context);
        // "block" CollectState::onRoundTable()
        return Result::Ignore;
    }

    Result PermanentCollectWriteState::onHash(SolverContext & context, const cs::Hash & hash, const cs::PublicKey & sender)
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
            /*
                if(Consensus::Log) {
                    constexpr const size_t hash_len = sizeof(sender.str) / sizeof(sender.str[0]);
                    LOG_WARN(name() << ": hash received do not match!!! Sender " << byteStreamToHex(sender.str, hash_len));
                }
              */ // vshilkin
            }
        }
        if(not_enough <= 0) {
            // received enough hashes
            if(Consensus::Log) {
                LOG_NOTICE(name() << ": request new round");
            }
            cancel_collect_hashes(context);
            context.spawn_next_round();
            return Result::Finish;
        }
        return Result::Ignore;
    }

    void PermanentCollectWriteState::create_and_send_block(SolverContext & context)
    {
        // fill block
        if(Consensus::Log) {
            LOG_NOTICE(name() << ": " << pool.transactions().size() << " transactions collected");
        }

        // send block
        context.create_and_send_new_block_from(pool);
        pown = std::make_unique<cs::Hash>(); // ((char*) (context.blockchain().getLastWrittenHash().to_binary().data())); vshilkin

        // clear pool
        pool = csdb::Pool {};

        start_collect_hashes(context);
    }

    void PermanentCollectWriteState::start_collect_transactions(SolverContext & context)
    {
        // left some time to receive hashes
        if(Consensus::Log) {
            LOG_NOTICE(name() << ": schedule to send block block of transactions in " << Consensus::T_coll_trans << " ms");
        }
        SolverContext * pctx = &context;
        tag_round_timeout = context.scheduler().InsertOnce(Consensus::T_coll_trans, [this, pctx]() {
            if(Consensus::Log) {
                LOG_NOTICE(name() << ": sending block");
            }
            create_and_send_block(*pctx);
            tag_round_timeout = CallsQueueScheduler::no_tag;
        });
    }

    void PermanentCollectWriteState::cancel_collect_transactions(SolverContext & context)
    {
        if(tag_round_timeout != CallsQueueScheduler::no_tag) {
            if(Consensus::Log) {
                LOG_NOTICE(name() << ": cancel send block");
            }
            tag_round_timeout = CallsQueueScheduler::no_tag;
            context.scheduler().Remove(tag_round_timeout);
        }
    }

    void PermanentCollectWriteState::start_collect_hashes(SolverContext & context)
    {
        if(Consensus::Log) {
            LOG_NOTICE(name() << ": start wait hashes for " << Consensus::T_hash << " ms");
        }
        SolverContext * pctx = &context;
        tag_hashes_timeout = context.scheduler().InsertOnce(Consensus::T_hash, [this, pctx]() {
            // time to wait for hashes is expired => spawn new round if hashes enough
            if(pctx->cnt_hash_recv() >= Consensus::MinTrustedNodes) {
                // we have got minimal required count of hashes, so we can spawn new round
                if(Consensus::Log) {
                    LOG_WARN(name() << ": we havent got all desired hashes but enough to request new round");
                }
                pctx->spawn_next_round();
            }
            tag_hashes_timeout = CallsQueueScheduler::no_tag;
        });
    }

    void PermanentCollectWriteState::cancel_collect_hashes(SolverContext & context)
    {
        if(tag_hashes_timeout != CallsQueueScheduler::no_tag) {
            if(Consensus::Log) {
                LOG_NOTICE(name() << ": cancel wait hashes");
            }
            tag_hashes_timeout = CallsQueueScheduler::no_tag;
            context.scheduler().Remove(tag_hashes_timeout);
        }
    }

} // slv2
