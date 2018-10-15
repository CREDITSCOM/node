#include "NormalState.h"
#include "../SolverContext.h"
#include "../Consensus.h"
#include "../Blockchain.h"
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
            // only once executed block:
            if(target_wallets.empty()) {
                // generate fake target key(s)
                uint8_t sk[SecretKeySize];
                uint8_t pk[PublicKeySize];
                for(int i = 0; i < CountTargetWallets; i++) {
                    crypto_sign_keypair(pk, sk);
                    csdb::Address pub = csdb::Address::from_public_key((const char*) pk);
                    target_wallets.push_back(pub);
                    if(Consensus::Log) {
                        LOG_NOTICE(name() << ": target address " << pub.to_string());
                    }
                }
                // generate fake own keys
                crypto_sign_keypair(pk, own_secret_key);
                own_wallet = csdb::Address::from_public_key((const char*) pk);
                if(Consensus::Log) {
                    LOG_NOTICE(name() << ": source address " << own_wallet.to_string());
                }
                is_spam_balance_valid = check_spammer_balance(context);
            }

            // in fact, pctx ic not less "alive" as a context.scheduler itself :-)
            if(Consensus::Log) {
                LOG_NOTICE(name() << ": started spam transactions every " << T_spam_trans << " msec");
            }
            tag_spam = context.scheduler().InsertPeriodic(T_spam_trans, [this, pctx]() {
                if(!is_spam_balance_valid) {
                    if(Consensus::Log) {
                        LOG_DEBUG(name() << ": insufficient balance to spam transaction");
                    }
                    return;
                }
                csdb::Transaction tr;
                setup(tr, *pctx);
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
            is_spam_balance_valid = check_spammer_balance(context);
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

    void NormalState::setup(csdb::Transaction& tr, SolverContext& context)
    {
        // based on Solver::spamWithTransactions()

		tr.set_source(context.optimize(own_wallet));
		tr.set_target(context.optimize(*(target_wallets.cbegin() + spam_index)));
		tr.set_innerID(++spam_counter + CountTransInRound * context.round());

        tr.set_currency(1);
		tr.set_amount(csdb::Amount(randFT(1, 100), 0));
        tr.set_max_fee(csdb::AmountCommission(0.1));

        // sign the transaction with own secret key:
        sign(tr);

        ++spam_index;
        if(spam_index >= target_wallets.size()) {
            spam_index = 0;
        }
    }

    void NormalState::sign(csdb::Transaction& tr)
    {
        unsigned char sig[SecretKeySize];
        unsigned long long sig_size = 0;
        auto b = tr.to_byte_stream_for_sig();
        crypto_sign_ed25519_detached(sig, &sig_size, b.data(), b.size(), own_secret_key);
        tr.set_signature(std::string(&sig[0], &sig[0] + sig_size));
    }

    bool NormalState::check_spammer_balance(SolverContext& context)
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
            // в генезис-блоке на start (innerID 0) и на спамер (innerID 1) размещено по 100 000 000 в валюте (1)
            // пробуем взять оттуда
            csdb::Transaction tr;
            tr.set_source(context.optimize(context.address_spammer()));
            tr.set_target(context.optimize(own_wallet));
            tr.set_currency(1);
            tr.set_amount(csdb::Amount(deposit, 0));
            tr.set_max_fee(csdb::AmountCommission(max_fee));
            //TODO: get unique increasing through different nodes ID for the spam_wallet -> own_wallet 1st transaction:
            tr.set_innerID(context.blockchain().getLastWrittenSequence() + 1);

            // do not sign the transaction from spammer wallet to ours:
            //sign(tr);

            context.add(tr);
            if(Consensus::Log) {
                LOG_NOTICE(name() << ": spammer wallet balance is " << static_cast<int>(balance) << ", trying to deposit " << deposit);
            }
            return false;
        }
        if(Consensus::Log) {
            // this allow print message once after deposit done
            if(!is_spam_balance_valid) {
                LOG_NOTICE(name() << ": spammer wallet balance " << static_cast<int>(balance) << " is enough to spam transactions");
            }
        }
        return true;
    }

} // slv2
