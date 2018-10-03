#include "NormalState.h"
#include "../SolverContext.h"
#include "../Node.h"
#include <csdb/address.h>
#include <csdb/currency.h>
#include <lib/system/logger.hpp>

namespace slv2
{
    void NormalState::on(SolverContext& context)
    {
        DefaultStateBehavior::on(context);

        SolverContext * pctx = &context;

        if(context.is_spammer()) {
            // in fact, pctx ic not less "alive" as a context.scheduler itself :-)
            if(Consensus::Log) {
                LOG_NOTICE(name() << ": started spam transactions every " << T_spam_trans << " msec");
            }
            tag_spam = context.scheduler().InsertPeriodic(T_spam_trans, [this, pctx]() {
                csdb::Transaction tr;
                setup(&tr, pctx);
                pctx->add(tr);
                if(Consensus::Log) {
                    LOG_DEBUG(name() << ": added spam transaction");
                }
            }, true);
        }

        if(Consensus::Log) {
            LOG_NOTICE(name() << ": started flush transactions every " << Consensus::T_coll_trans << " msec");
        }
        tag_flush = context.scheduler().InsertPeriodic(Consensus::T_coll_trans, [this, pctx]() {
            pctx->flush_transactions();
            if(Consensus::Log) {
                LOG_DEBUG(name() << ": flushing transactions");
            }
        }, true);

    }

    void NormalState::off(SolverContext& context)
    {
        if(CallsQueueScheduler::no_tag != tag_spam) {
            if(Consensus::Log) {
                LOG_NOTICE(name() << ": stop spam transactions");
            }
            context.scheduler().Remove(tag_spam);
            tag_spam = CallsQueueScheduler::no_tag;
        }

        if(CallsQueueScheduler::no_tag != tag_flush) {
            if(Consensus::Log) {
                LOG_NOTICE(name() << ": stop flush transactions");
            }
            context.scheduler().Remove(tag_flush);
            tag_flush = CallsQueueScheduler::no_tag;
        }
    }

    Result NormalState::onBlock(SolverContext & context, csdb::Pool & block, const PublicKey & sender)
    {
        Result res = DefaultStateBehavior::onBlock(context, block, sender);
        if(res == Result::Finish) {
            Hash test_hash((char*) (context.node().getBlockChain().getLastWrittenHash().to_binary().data()));
            if(Consensus::Log) {
                LOG_NOTICE(name() << ": sending hash in reply to block sender");
            }
            // in case of some block was restored from inner cache the last_block_sender contains correct value, other then
            // argument sender value
            context.node().sendHash(test_hash, DefaultStateBehavior::last_block_sender);
        }
        return res;
    }

    int NormalState::randFT(int min, int max)
    {
        return rand() % (max - min + 1) + min;
    }

    void NormalState::setup(csdb::Transaction * ptr, SolverContext * pctx)
    {
        auto aaa = csdb::Address::from_string("0000000000000000000000000000000000000000000000000000000000000001");
        //auto bbb = csdb::Address::from_string("0000000000000000000000000000000000000000000000000000000000000002");
        uint64_t iid = 0;
        ptr->set_target(aaa);
        ptr->set_source(csdb::Address::from_public_key((char*) pctx->public_key().data()));
        ptr->set_currency(csdb::Currency("CS"));
        ptr->set_amount(csdb::Amount(randFT(1, 1000), 0));
        ptr->set_max_fee(csdb::Amount(0, 1, 10));
        ptr->set_balance(csdb::Amount(ptr->amount().integral() + 2, 0));
        ptr->set_innerID(iid);
    }

} // slv2
