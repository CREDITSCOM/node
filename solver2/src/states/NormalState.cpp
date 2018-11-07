#include <states/NormalState.h>
#include <SolverContext.h>
#include <Consensus.h>

#pragma warning(push)
//#pragma warning(disable: 4267 4244 4100 4245)
#include <csnode/blockchain.hpp>
#pragma warning(pop)

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

        // if we were Writer un the previous round, we have a deferred block, flush it:
        if(context.is_block_deferred()) {
            context.flush_deferred_block();
        }

        SolverContext * pctx = &context;

        if(context.is_spammer()) {
            // in fact, pctx ic not less "alive" as a context.scheduler itself :-)
            if(Consensus::Log) {
                LOG_NOTICE(name() << ": started spam transactions every " << T_spam_trans << " msec");
            }
            tag_spam = context.scheduler().InsertPeriodic(T_spam_trans, [this, pctx]() {spam_transaction(*pctx);}, true);
        }
    }

    void NormalState::off(SolverContext& context)
    {
        if(CallsQueueScheduler::no_tag != tag_spam) {
            if(Consensus::Log) {
                LOG_NOTICE(name() << ": stop spam transactions (" << std::min(spam_counter, CountTransInRound) << " added)");
            }
            context.scheduler().Remove(tag_spam);
            tag_spam = CallsQueueScheduler::no_tag;
        }
    }

    void NormalState::onRoundEnd(SolverContext & context, bool is_bigbang)
    {
        DefaultStateBehavior::onRoundEnd(context, is_bigbang);
        spam_counter = 0;
    }

    Result NormalState::onRoundTable(SolverContext & context, const size_t round)
    {
        // test own balance
        if(context.is_spammer()) {
            spam_counter = 0;
            is_spam_balance_valid = check_spammer_balance(context);
        }
        return DefaultStateBehavior::onRoundTable(context, round);
    }

    void NormalState::spam_transaction(SolverContext& context)
    {
        ++spam_counter;
        if(1 == spam_counter) {
            // first call after on() tries to deposit some value
            is_spam_balance_valid = check_spammer_balance(context);

            if(!is_spam_balance_valid) {
                if(Consensus::Log) {
                    LOG_DEBUG(name() << ": insufficient balance to spam transaction");
                }
                return;
            }
        }

        if(!is_spam_balance_valid) {
            return;
        }
        if(spam_counter >= CountTransInRound) {
            if(Consensus::Log) {
                if(spam_counter == CountTransInRound) {
                    LOG_WARN(name() << ": maximum trans/round (" << CountTransInRound << ") reached");
                }
            }
            return;
        }
        for(int i = 0; i < MinCountTrans; i++) {
            csdb::Transaction tr;
            setup(tr, context);
            context.add(tr);
            if(Consensus::Log) {
                LOG_DEBUG(name() << ": added spam transaction " << tr.innerID());
            }
        }
    }

    int NormalState::randFT(int min, int max)
    {
        return rand() % (max - min + 1) + min;
    }

    int64_t NormalState::next_inner_id(size_t round)
    {
        return (++spam_counter + CountTransInRound * round);
    }

    void NormalState::setup(csdb::Transaction& tr, SolverContext& context)
    {
        // based on Solver::spamWithTransactions()

		tr.set_source(context.optimize(own_wallet));
		tr.set_target(context.optimize(*(target_wallets.cbegin() + spam_index)));
		tr.set_innerID(next_inner_id(context.round()));

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

    Result NormalState::onBlock(SolverContext & context, csdb::Pool & block, const cs::PublicKey & sender)
    {
        auto r = DefaultStateBehavior::onBlock(context, block, sender);
        if(context.is_block_deferred()) {
            context.flush_deferred_block();
        }
        return r;
    }

    bool NormalState::check_spammer_balance(SolverContext& context)
    {
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
        }

        BlockChain::WalletData wd;
        csdb::internal::WalletId id;
        double balance = 0;
        if(context.blockchain().findWalletData(own_wallet, wd, id)) {
            balance = wd.balance_.to_double();
        }
        if(balance < 1000.0) {
            if(context.transaction_still_in_pool(deposit_inner_id)) {
                LOG_NOTICE(name() << ": spammer wallet balance is " << static_cast<int>(balance) << ", waiting to confirm deposit");
                return false;
            }

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
            deposit_inner_id = next_inner_id(context.round());
            tr.set_innerID(deposit_inner_id);

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
