#include <states/TrustedStage1State.h>
#include <SolverContext.h>
#include <Consensus.h>

#pragma warning(push)
#pragma warning(disable: 4267 4244 4100 4245)
#include <Solver/Solver.hpp>
#pragma warning(pop)

#include <csnode/blockchain.hpp>
#include <csnode/conveyer.hpp>
#include <lib/system/logger.hpp>
#include <lib/system/utils.hpp>

#if LOG_LEVEL & FLAG_LOG_DEBUG
#include <sstream>
#endif
#pragma warning(push)
#pragma warning(disable: 4324)
#include <sodium.h>
#pragma warning(pop)

namespace slv2
{
    void TrustedStage1State::on(SolverContext& context)
    {
        DefaultStateBehavior::on(context);

        // if we were Writer un the previous round, we have a deferred block, flush it:
        if (context.is_block_deferred()) {
          context.flush_deferred_block();
        }

        memset(&stage, 0, sizeof(stage));
        stage.sender = (uint8_t) context.own_conf_number();
        enough_hashes = false;
        transactions_checked = false;

        if(Consensus::Log) {
            LOG_NOTICE(name() << ": I am [" << context.own_conf_number() << "]");
        }
    }

    void TrustedStage1State::off(SolverContext & context)
    {
        LOG_NOTICE(name() << ": --> stage-1 [" << (int) stage.sender << "]");
        context.add_stage1(stage, true);
    }

    void TrustedStage1State::onRoundEnd(SolverContext& context, bool is_bigbang)
    {
        // in this stage we got round end only having troubles
        if(context.is_block_deferred()) {
            if(is_bigbang) {
                context.drop_deferred_block();
            }
            else {
                context.flush_deferred_block();
            }
        }
    }

    Result TrustedStage1State::onTransactionList(SolverContext & context, csdb::Pool & pool)
    {
        if(Consensus::Log) {
            LOG_NOTICE(name() << ": <-- tr.list [" << pool.sequence() << "] of " << pool.transactions_count());
#if LOG_LEVEL & FLAG_LOG_DEBUG
            std::ostringstream os;
            for(const auto& t : p.transactions()) {
                os << " " << t.innerID();
            }
            LOG_DEBUG("SolverCore:" << os.str());
#endif // FLAG_LOG_DEBUG
        }

        // good transactions storage
        csdb::Pool accepted_pool {};
        // bad tansactions storage:
        csdb::Pool rejected_pool {};

        // TODO: update own hash vector?
        
        pool = removeTransactionsWithBadSignatures(context, pool);

        // see Solver::runCinsensus()
        cs::TransactionsPacket packet;
        cs::Conveyer& conveyer = cs::Conveyer::instance();
        for(const auto& hash : conveyer.roundTable().hashes) {
            const auto& hashTable = conveyer.transactionsPacketTable();
            if(!hashTable.count(hash)) {
                cserror() << name() << ": consensus build vector: HASH NOT FOUND";
                return Result::Failure;
            }
            const auto& transactions = conveyer.packet(hash).transactions();
            for(const auto& transaction : transactions) {
                if(!packet.addTransaction(transaction)) {
                    cserror() << name() << ": cannot add transaction to packet in consensus";
                }
            }
        }
        cslog() << name() << ": consensus transaction packet of " << packet.transactionsCount() << " transactions";
        context.update_fees(packet);
        auto result = context.build_vector(packet);
        if(Consensus::Log) {
            LOG_NOTICE(name() << ": accepted " << accepted_pool.transactions_count()
                << " trans, rejected " << rejected_pool.transactions_count());
        }

        std::copy(result.cbegin(), result.cend(), stage.hash.begin());

        context.accept_transactions(accepted_pool);
        transactions_checked = true;

        return (enough_hashes ? Result::Finish : Result::Ignore);
    }

    Result TrustedStage1State::onHash(SolverContext & context, const cs::Hash & hash, const cs::PublicKey & sender)
    {
        LOG_NOTICE(name() << ": <-- hash " << cs::Utils::byteStreamToHex(hash.data(), hash.size())
            << " from " << cs::Utils::byteStreamToHex(sender.data(), sender.size()));
        cs::Hash myHash;
        const auto& lwh = context.blockchain().getLastWrittenHash().to_binary();
        std::copy(lwh.cbegin(), lwh.cend(), myHash.begin());
        if(stage.candidatesAmount < Consensus::MinTrustedNodes) {
            if(hash == myHash) {
                bool keyFound = false;
                for(uint8_t i = 0; i < stage.candidatesAmount; i++) {
                    if(stage.candiates[i] == sender) {
                        keyFound = true;
                        break;
                    }
                }
                if(!keyFound) {
                    stage.candiates[stage.candidatesAmount] = sender;
                    stage.candidatesAmount += 1;
                }
            }
            else {
                // hash does not match to own hash
                return Result::Ignore;
            }
        }
        if(stage.candidatesAmount >= Consensus::MinTrustedNodes) {
            // enough hashes
            // flush deferred block to blockchain if any
            if(context.is_block_deferred()) {
                context.flush_deferred_block();
            }
            enough_hashes = true;
            return (transactions_checked ? Result::Finish : Result::Ignore);
        }
        return Result::Ignore;
    }

    csdb::Pool TrustedStage1State::removeTransactionsWithBadSignatures(SolverContext& context, const csdb::Pool& p)
    {
        csdb::Pool good;
        BlockChain::WalletData data_to_fetch_pulic_key;
        const BlockChain& bc = context.blockchain();
        bool force_permit_reported = false; // flag to report only once per round, not per every transaction
        for(const auto& tr : p.transactions()) {
            const auto& src = tr.source();
            csdb::internal::byte_array pk;
            if(src.is_wallet_id()) {
                bc.findWalletData(src.wallet_id(), data_to_fetch_pulic_key);
                pk.assign(data_to_fetch_pulic_key.address_.cbegin(), data_to_fetch_pulic_key.address_.cend());
            }
            else {
                const auto& tmpref = src.public_key();
                pk.assign(tmpref.cbegin(), tmpref.cend());
            }
            bool force_permit = (!force_permit_reported && context.is_spammer() && pk == context.address_spammer().public_key());
            if(force_permit || tr.verify_signature(pk)) {
                if(Consensus::Log) {
                    if(force_permit) {
                        force_permit_reported = true;
                        LOG_WARN(name() << ": permit drain " << static_cast<int>(tr.amount().to_double()) << " from spammer wallet ignoring check signature");
                    }
                }
                good.add_transaction(tr);
            }
        }
        if(Consensus::Log) {
            auto cnt_before = p.transactions_count();
            auto cnt_after = good.transactions_count();
            if(cnt_before != cnt_after) {
                LOG_WARN(name() << ": " << cnt_before - cnt_after << " trans. filtered while test signatures");
            }
        }
        return good;
    }

} // slv2

