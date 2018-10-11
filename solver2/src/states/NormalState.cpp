#include "NormalState.h"
#include "../SolverContext.h"
#include "../Consensus.h"
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

        SolverContext * pctx = &context;

        if(context.is_spammer()) {
            if(target_wallets.empty()) {
                uint8_t sk[64];
                uint8_t pk[32];
                for(int i = 0; i < CountTargetWallets; i++) {
                    crypto_sign_keypair(pk, sk);
                    csdb::Address pub = csdb::Address::from_public_key((const char*) pk);
                    target_wallets.push_back(pub);
                    if(Consensus::Log) {
                        LOG_NOTICE(name() << ": target address " << pub.to_string());
                    }
                }
                crypto_sign_keypair(pk, sk);
                own_wallet = csdb::Address::from_public_key((const char*) pk);
                if(Consensus::Log) {
                    LOG_NOTICE(name() << ": source address " << own_wallet.to_string());
                }
                check_spammer_balance(context);
            }

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
            LOG_NOTICE(name() << ": started flush transactions every " << Consensus::T_flush_trans << " msec");
        }
        tag_flush = context.scheduler().InsertPeriodic(Consensus::T_flush_trans, [this, pctx]() {
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
        // test own balance
        if(context.is_spammer()) {
            check_spammer_balance(context);
        }
        return DefaultStateBehavior::onRoundTable(context, round);
    }

    Result NormalState::onBlock(SolverContext & context, csdb::Pool & block, const PublicKey & sender)
    {
        Result res = DefaultStateBehavior::onBlock(context, block, sender);
        if(res == Result::Finish) {
            DefaultStateBehavior::sendLastWrittenHash(context, sender);
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

		ptr->set_source(pctx->optimize(own_wallet));
		ptr->set_target(pctx->optimize(*(target_wallets.cbegin() + spam_index)));
		ptr->set_innerID(++spam_counter + CountTransInRound * pctx->round());

		ptr->set_amount(csdb::Amount(randFT(1, 100), 0));
        ptr->set_max_fee(csdb::AmountCommission(0.1));

        ++spam_index;
        if(spam_index >= target_wallets.size()) {
            spam_index = 0;
        }
    }

    void NormalState::check_spammer_balance(SolverContext& context)
    {
        BlockChain::WalletData wd;
        csdb::internal::WalletId id;
        double balance = 0;
        if(context.blockchain().findWalletData(own_wallet, wd, id)) {
            balance = wd.balance_.to_double();
        }
        if(balance < 1000.0) {
            constexpr const int32_t deposit = 1'000'000;
            constexpr const double max_fee = 0.1;
            // deposit required
            // в генезис-блоке на start (innerID 0) и (if defined(SPAMMER)) на спамер (innerID 1) размещено по 100 000 000 в валюте (1)
            // пробуем взять оттуда
            csdb::Transaction tr;
            tr.set_source(context.optimize(context.address_spammer()));
            tr.set_target(context.optimize(own_wallet));
            tr.set_currency(1);
            tr.set_amount(csdb::Amount(deposit, 0));
            tr.set_max_fee(csdb::AmountCommission(max_fee));
            //TODO: get unique increasing through different nodes ID for the spam_wallet -> own_wallet 1st transaction:
            tr.set_innerID(context.blockchain().getLastWrittenSequence() + 1);
            context.add(tr);
            if(Consensus::Log) {
                LOG_NOTICE(name() << ": spammer wallet balance is " << balance << ", trying to deposit " << deposit);
            }
        }
    }

} // slv2
