#include "NormalState.h"
#include "../SolverContext.h"
#include "../Node.h"
#include <csdb/address.h>
#include <csdb/currency.h>
#include <csdb/amount.h>
#include <csdb/amount_commission.h>
#include <lib/system/logger.hpp>

#pragma warning(push)
#pragma warning(disable: 4324)
#include <sodium.h>
#pragma warning(pop)

namespace slv2
{

    void NormalState::on(SolverContext& context)
    {
        DefaultStateBehavior::on(context);

        if(context.is_spammer()) {
            if(spam_keys.empty()) {
                uint8_t sk [64];
                uint8_t pk [32];
                for(int i = 0; i < CountSpamKeysVariants; i++) {
                    crypto_sign_keypair(pk, sk);
                    csdb::Address pub = csdb::Address::from_public_key((const char*) pk);
                    spam_keys.push_back(pub);
                    if(Consensus::Log) {
                        LOG_NOTICE(name() << ": target address " << pub.to_string());
                    }
                }
                crypto_sign_keypair(pk, sk);
                own_key = csdb::Address::from_public_key((const char*) pk);
                if(Consensus::Log) {
                    LOG_NOTICE(name() << ": source address " << own_key.to_string());
                }
            }

            csdb::Transaction tr;            
            tr.set_target( csdb::Address::from_public_key((char*) context.public_key().data()));
            tr.set_source(context.address_start());
            tr.set_currency(1);
            tr.set_amount(csdb::Amount(10000, 0));
            tr.set_balance(csdb::Amount(10000000, 0));
            tr.set_innerID(1);
            context.add(tr);
        }

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
                    LOG_DEBUG(name() << ": added spam transaction " << tr.innerID());
                }
            }, true);
        }

        if(Consensus::Log) {
            LOG_NOTICE(name() << ": started flush transactions every " << Consensus::T_coll_trans << " msec");
        }
        tag_flush = context.scheduler().InsertPeriodic(Consensus::T_coll_trans, [this, pctx]() {
            if(Consensus::Log) {
                LOG_DEBUG(name() << ": flushing transactions");
            }
            flushed_counter += pctx->flush_transactions();
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

    void NormalState::onRoundEnd(SolverContext & context)
    {
        // send not yet flushed transactions if any
        flushed_counter += context.flush_transactions();
        if(Consensus::Log) {
            LOG_NOTICE(name() << ": " << flushed_counter << " transactions where flushed during round");
        }
        flushed_counter = 0;
        DefaultStateBehavior::onRoundEnd(context);

        spam_counter = 0;
    }

    Result NormalState::onRoundTable(SolverContext & context, const uint32_t round)
    {
        flushed_counter = 0;
        return DefaultStateBehavior::onRoundTable(context, round);
    }

    Result NormalState::onBlock(SolverContext & context, csdb::Pool & block, const PublicKey & sender)
    {
        Result res = DefaultStateBehavior::onBlock(context, block, sender);
        if(res == Result::Finish) {
            Hash test_hash((char*) (context.node().getBlockChain().getLastWrittenHash().to_binary().data()));
            if(Consensus::Log) {
                LOG_NOTICE(name() << ": sending hash in reply to block sender");
            }
            context.node().sendHash(test_hash, sender);
        }
        return res;
    }

    int NormalState::randFT(int min, int max)
    {
        return rand() % (max - min + 1) + min;
    }

    void NormalState::setup(csdb::Transaction * ptr, SolverContext * pctx)
    {
        // based on Solver::spamWithTransactions()

        Node& node = pctx->node();
        csdb::internal::WalletId id;

        if(node.getBlockChain().findWalletId(own_key, id)) {
            ptr->set_source(csdb::Address::from_wallet_id(id));
        }
        else {
            ptr->set_source(own_key);
        }
        
        //TODO: does not accepted by other nodes
        if(node.getBlockChain().findWalletId(*(spam_keys.cbegin() + spam_index), id)) {
            ptr->set_target(csdb::Address::from_wallet_id(id));
        }
        else {
            ptr->set_target(*(spam_keys.cbegin() + spam_index));
        }
        
        ptr->set_amount(csdb::Amount(randFT(1, 1000), 0));
        ptr->set_max_fee(csdb::AmountCommission(0.1));
        ptr->set_innerID(++spam_counter + CountTransInRound * pctx->round());

        ++spam_index;
        if(spam_index >= spam_keys.size()) {
            spam_index = 0;
        }
    }

} // slv2
