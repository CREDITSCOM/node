#include "NormalState.h"
#include "../SolverContext.h"
#include <csdb/address.h>
#include <csdb/currency.h>

#include <iostream>

namespace slv2
{
    void NormalState::on(SolverContext& context)
    {
        SolverContext * pctx = &context;

        if(context.is_spammer()) {
            // in fact, pctx ic not less "alive" as a context.scheduler itself :-)
            tag_spam = context.scheduler().InsertPeriodic(T_spam_trans, [this, pctx]() {
                csdb::Transaction tr;
                setup(&tr, pctx);
                pctx->add(tr);
            }, true);
        }

        tag_flush = context.scheduler().InsertPeriodic(Consensus::T_flush_trans, [pctx]() {
            pctx->flush_transactions();
        }, true);

    }

    void NormalState::off(SolverContext& context)
    {
        if(CallsQueueScheduler::no_tag != tag_spam) {
            context.scheduler().Remove(tag_spam);
            tag_spam = CallsQueueScheduler::no_tag;
        }

        if(CallsQueueScheduler::no_tag != tag_flush) {
            context.scheduler().Remove(tag_flush);
            tag_flush = CallsQueueScheduler::no_tag;
        }
    }

    Result NormalState::onRoundTable(SolverContext& /*context*/, const uint32_t round)
    {
        std::cout << name() << ": round table received: " << round << std::endl;
        return Result::Finish;
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
