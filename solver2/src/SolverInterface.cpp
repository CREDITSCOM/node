#include "SolverCore.h"
#include "Consensus.h"
#include "SolverCompat.h"
#include "Node.h"
#include "Generals.h"
#include <csdb/currency.h>
#include <lib/system/logger.hpp>

namespace slv2
{

    const Credits::HashVector& SolverCore::getMyVector() const
    {
        if(opt_is_proxy_v1 && pslv_v1) {
            return pslv_v1->getMyVector();
        }
        if(!pown_hvec) {
            // empty one is for test purpose
            static Credits::HashVector stub {};
            return stub;
        }
        return *pown_hvec;
    }

    const Credits::HashMatrix& SolverCore::getMyMatrix() const
    {
        if(opt_is_proxy_v1 && pslv_v1) {
            return pslv_v1->getMyMatrix();
        }
        if(!pgen) {
            // empty one is for test purpose
            static Credits::HashMatrix stub {};
            return stub;
        }
        return pgen->getMatrix();
    }

    void SolverCore::set_keys(const KeyType& pub, const KeyType& priv)
    {
        if(opt_is_proxy_v1 && pslv_v1) {
            pslv_v1->set_keys(pub, priv);
        }
        public_key = pub;
        private_key = priv;
    }

    void SolverCore::addInitialBalance()
    {
        if(opt_is_proxy_v1 && pslv_v1) {
            pslv_v1->addInitialBalance();
            return;
        }

        // copied from original solver-1
#if defined(ADD_INITIAL_BALANCE)
        LOG_DEBUG("===SETTING DB===");
        const std::string start_address = "0000000000000000000000000000000000000000000000000000000000000002";
        csdb::Transaction tr;
        tr.set_target(csdb::Address::from_public_key((char*) public_key.data()));
        tr.set_source(csdb::Address::from_string(start_address));
        tr.set_currency(csdb::Currency(1));
        tr.set_amount(csdb::Amount(10000, 0));
        tr.set_balance(csdb::Amount(10000000, 0));
        tr.set_innerID(1);
        send_wallet_transaction(tr);
        if(Consensus::Log) {
            LOG_NOTICE("SolverCore: initial balance added");
        }
#endif // ADD_INITIAL_BALANCE
    }

    void SolverCore::setBigBangStatus(bool status)
    {
        if(opt_is_proxy_v1 && pslv_v1) {
            pslv_v1->setBigBangStatus(status);
            return;
        }

        is_bigbang = status;
        if(!pstate) {
            return;
        }
        if(status) {
            handleTransitions(Event::BigBang);
        }
    }

    void SolverCore::gotTransaction(const csdb::Transaction& trans)
    {
        if(opt_is_proxy_v1 && pslv_v1) {
            csdb::Transaction tmp = trans;
            pslv_v1->gotTransaction(std::move(tmp));
            return;
        }

        if(!pstate) {
            return;
        }
        // produces too much output:
        if(Consensus::Log) {
            LOG_DEBUG("SolverCore: got transaction " << trans.innerID() << " from " << trans.source().to_string());
        }
        if(stateCompleted(pstate->onTransaction(*pcontext, trans))) {
            handleTransitions(Event::Transactions);
        }
    }

    void SolverCore::gotTransactionList(csdb::Pool& p)
    {
        if(opt_is_proxy_v1 && pslv_v1) {
            csdb::Pool tmp = p;
            pslv_v1->gotTransactionList(std::move(tmp));
            return;
        }

        auto tl_seq = p.sequence();
        if(tl_seq == last_trans_list_recv) {
            // already received
            if(Consensus::Log) {
                LOG_WARN("SolverCore: transaction list (#" << tl_seq << ") already received, ignore");
            }
            return;
        }
        last_trans_list_recv = tl_seq;

        // чистим для нового списка
        pool = csdb::Pool {};

        if(Consensus::Log) {
            LOG_NOTICE("SolverCore: transaction list (#" << tl_seq << ", " << p.transactions_count() <<" transactions) received, updating own hashvector");
			std::ostringstream os;
			for (const auto& t : p.transactions()) {
				os << " " << t.innerID();
			}
			LOG_DEBUG("SolverCore:" << os.str());
        }
        // bad tansactions storage:
        csdb::Pool b_pool {};
        // update own hash vector
        if(pnode != nullptr && pgen != nullptr) {
            if(!opt_spammer_on) {
                p = removeTransactionsWithBadSignatures(p);
            }
            pfee->CountFeesInPool(pnode, &p);
            auto result = pgen->buildvector(p, pool, b_pool);
            if(Consensus::Log) {
                LOG_NOTICE("SolverCore: " << pool.transactions_count() << " are valid, " << b_pool.transactions_count() << " moved to bad pool");
            }
            pown_hvec->Sender = pnode->getMyConfNumber();
            pown_hvec->hash = result;
        }

        if(!pstate) {
            return;
        }
        if(stateCompleted(pstate->onTransactionList(*pcontext, p))) {
            handleTransitions(Event::Transactions);
        }
    }

    void SolverCore::gotVector(const Credits::HashVector& vect)
    {
        if(opt_is_proxy_v1 && pslv_v1) {
            Credits::HashVector tmp = vect;
            pslv_v1->gotVector(std::move(tmp));
            return;
        }

        if(!pstate) {
            return;
        }
        //TODO: how to get real public key from vect.Sender?
        if(Consensus::Log) {
            LOG_DEBUG("SolverCore: gotVector()");
        }
        if(stateCompleted(pstate->onVector(*pcontext, vect, PublicKey {}))) {
            handleTransitions(Event::Vectors);
        }
    }

    void SolverCore::gotMatrix(const Credits::HashMatrix& matr)
    {
        if(opt_is_proxy_v1 && pslv_v1) {
            Credits::HashMatrix tmp = matr;
            pslv_v1->gotMatrix(std::move(tmp));
            return;
        }

        if(!pstate) {
            return;
        }
        //TODO: how to get real public key from vect.Sender?
        if(Consensus::Log) {
            LOG_DEBUG("SolverCore: gotMatrix()");
        }
        if(stateCompleted(pstate->onMatrix(*pcontext, matr, PublicKey {}))) {
            handleTransitions(Event::Matrices);
        }
    }

    void SolverCore::gotBlock(csdb::Pool& p, const PublicKey& sender)
    {
        if(opt_is_proxy_v1 && pslv_v1) {
            csdb::Pool tmp = p;
            pslv_v1->gotBlock(std::move(tmp), sender);
            return;
        }

        // solver-1 logic: clear bigbang status upon block receive
        is_bigbang = false;

        if(!pstate) {
            return;
        }
        if(Consensus::Log) {
            LOG_DEBUG("SolverCore: gotBlock()");
        }
        if(stateCompleted(pstate->onBlock(*pcontext, p, sender))) {
            handleTransitions(Event::Block);
        }
        // makes subsequent calls to pstate->onBlock() if find appropriate next blocks in cache:
        test_outrunning_blocks();
    }

    void SolverCore::gotBlockRequest(const csdb::PoolHash& p_hash, const PublicKey& sender)
    {
        if(opt_is_proxy_v1 && pslv_v1) {
            csdb::PoolHash tmp = p_hash;
            pslv_v1->gotBlockRequest(std::move(tmp), sender);
            return;
        }

        if(Consensus::Log) {
            LOG_DEBUG("SolverCore: gotBlockRequest()");
        }
        // state does not take part
        if(pnode != nullptr) {
            csdb::Pool p = pnode->getBlockChain().loadBlock(p_hash);
            if(p.is_valid()) {
                p.set_previous_hash(csdb::PoolHash::from_string(""));
                pnode->sendBlockReply(p, sender);
            }
        }
    }

    void SolverCore::gotBlockReply(csdb::Pool& p)
    {
        if(opt_is_proxy_v1 && pslv_v1) {
            pslv_v1->gotBlockReply(std::move(p));
            return;
        }

        if(Consensus::Log) {
            LOG_DEBUG("SolverCore: gotBlockReply()");
        }
        LOG_DEBUG("SolverCore: got block on my request: " << p.sequence());
        if(p.sequence() == pnode->getBlockChain().getLastWrittenSequence() + 1) {
            pnode->getBlockChain().writeNewBlock(p);
        }
    }

    void SolverCore::gotHash(const Hash& hash, const PublicKey& sender)
    {
        if(opt_is_proxy_v1 && pslv_v1) {
            Hash modifiable = hash;
            pslv_v1->gotHash(modifiable, sender);
            return;
        }

        if(!pstate) {
            return;
        }
        if(Consensus::Log) {
            LOG_DEBUG("SolverCore: gotHash()");
        }
        if(stateCompleted(pstate->onHash(*pcontext, hash, sender))) {
            handleTransitions(Event::Hashes);
        }
    }

    void SolverCore::gotIncorrectBlock(csdb::Pool&& p, const PublicKey& sender)
    {
        if(opt_is_proxy_v1 && pslv_v1) {
            pslv_v1->gotIncorrectBlock(std::move(p), sender);
            return;
        }

        // store outrunning block for future using
        const auto seq = p.sequence();
        if(outrunning_blocks.count(seq) == 0) {
            outrunning_blocks [seq] = std::make_pair(p, sender);
            if(Consensus::Log) {
                LOG_NOTICE("SolverCore: got outrunning block #" << seq << ", cache it for future using");
            }
        }
        else {
            if(Consensus::Log) {
                LOG_DEBUG("SolverCore: gotIncorrectBlock(" << seq << "), ignored duplicated");
            }
        }
    }

    void SolverCore::tmpStorageProcessing()
    {
        if(opt_is_proxy_v1 && pslv_v1) {
            pslv_v1->tmpStorageProcessing();
            return;
        }

        if(Consensus::Log) {
            LOG_DEBUG("SolverCore: tmpStorageProcessing()");
        }
        test_outrunning_blocks();
    }

    void SolverCore::gotFreeSyncroBlock(csdb::Pool&& p)
    {
        if(opt_is_proxy_v1 && pslv_v1) {
            pslv_v1->gotFreeSyncroBlock(std::move(p));
            return;
        }

        // exactly acts as solver-1 analog
        const auto seq = p.sequence();
        if(rnd_storage.count(seq) == 0) {
            rnd_storage [seq] = p;
            if(Consensus::Log) {
                LOG_NOTICE("SolverCore: gotFreeSyncroBlock(" << seq << "), cached in rnd storage");
            }
        }
        else {
            if(Consensus::Log) {
                LOG_DEBUG("SolverCore: gotFreeSyncroBlock(" << seq << "), ignored duplicated");
            }
        }
    }

    void SolverCore::rndStorageProcessing()
    {
        if(opt_is_proxy_v1 && pslv_v1) {
            pslv_v1->rndStorageProcessing();
            return;
        }
        
        if(Consensus::Log) {
            LOG_DEBUG("SolverCore: rndStorageProcessing()");
        }
        // acts as solver-1 analog but removes outdated blocks from cache
        auto& bc = pnode->getBlockChain();
        while(! rnd_storage.empty()) {
            size_t new_seq = bc.getLastWrittenSequence() + 1;
            auto oldest = rnd_storage.begin();
            if(oldest->first < new_seq) {
                // erase outdated block
                rnd_storage.erase(oldest);
                continue;
            }
            else if(oldest->first == new_seq) {
                // store block and remove it from cache
                bc.putBlock(oldest->second);
                rnd_storage.erase(oldest);
            }
            else {
                // stop processing while get required block
                break;
            }
        }
    }

    void SolverCore::addConfirmation(uint8_t own_conf_number)
    {
        if(opt_is_proxy_v1 && pslv_v1) {
            pslv_v1->addConfirmation(own_conf_number);
            return;
        }

        if(Consensus::Log) {
            LOG_ERROR("SolverCore: addConfirmation(): not implemented yet");
        }
        if(!pstate) {
            return;
        }
    }

    void SolverCore::beforeNextRound()
    {
        if(opt_is_proxy_v1 && pslv_v1) {
            pslv_v1->beforeNextRound();
            return;
        }
        
        if(!pstate) {
            return;
        }
        pstate->onRoundEnd(*pcontext);
    }

    void SolverCore::nextRound()
    {
        if(opt_is_proxy_v1 && pslv_v1) {
            pslv_v1->nextRound();
            return;
        }

        // as store result of current round:
        if(Consensus::Log) {
            LOG_NOTICE("SolverCore: clear all stored senders (vectors, matrices, hashes)");
        }

        recv_vect.clear();
        recv_matr.clear();
        recv_hash.clear();

        if(!pstate) {
            return;
        }
        if(pnode != nullptr) {
            cur_round = pnode->getRoundNumber();
        }
        if(Consensus::Log) {
            LOG_NOTICE("SolverCore: nextRound()");
        }

        // update desire count of trusted nodes
        size_t cnt_trusted = pnode->getConfidants().size();
        if(cnt_trusted > cnt_trusted_desired) {
            cnt_trusted_desired = cnt_trusted;
        }

        if(stateCompleted(pstate->onRoundTable(*pcontext, cur_round))) {
            handleTransitions(Event::RoundTable);
        }

        //TODO: not good solution, to reproduce solver-1 logic only:
        if(is_bigbang) {
            csdb::Pool tmp {};
            gotTransactionList(tmp);
        }
    }

    void SolverCore::send_wallet_transaction(const csdb::Transaction& tr)
    {
        if(opt_is_proxy_v1 && pslv_v1) {
            pslv_v1->send_wallet_transaction(tr);
            return;
        }

        // thread-safe with flushTransactions(), suppose to receive calls from network-related threads
        std::lock_guard<std::mutex> l(trans_mtx);
        //TODO: such a way transactions added in solver-1, ask author about it
        transactions.transactions().push_back(tr);
        transactions.recount();
        if(Consensus::Log) {
            LOG_DEBUG("SolverCore: transaction " << tr.innerID() << " added, total " << transactions.transactions().size());
        }
    }

} // slv2
