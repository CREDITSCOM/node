#include "SolverCore.h"
#include "Consensus.h"
#include "SolverCompat.h"
#include "Node.h"
#include "Generals.h"
#include <Solver/Fee.h>
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
            auto result = pgen->buildvector(p, block_pool, b_pool);
            if(Consensus::Log) {
                LOG_NOTICE("SolverCore: " << block_pool.transactions_count() << " trans stored to block, " << b_pool.transactions_count() << " to bad pool");
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

        // solver-1: caching, actually duplicates before done caching in Node::getBlock()
        if(p.sequence() > cur_round) {
            gotIncorrectBlock(std::move(p), sender); // remove this line when the block candidate signing of all trusted will be implemented
            return;
        }

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
            LOG_DEBUG("SolverCore: got block on my request: " << p.sequence());
        }
        if(p.sequence() == pnode->getBlockChain().getLastWrittenSequence() + 1) {
            storeReceivedBlock(p);
        }
        else {
            gotIncorrectBlock(std::move(p), PublicKey {});
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

        gotIncorrectBlock(std::move(p), PublicKey {});
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

        test_outrunning_blocks();
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

        // чистим для нового списка
        block_pool = csdb::Pool {};

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

        // update desired count of trusted nodes
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
        trans_pool.transactions().push_back(tr);
        trans_pool.recount();
        if(Consensus::Log) {
            LOG_DEBUG("SolverCore: transaction " << tr.innerID() << " added, total " << trans_pool.transactions_count());
        }
    }

    csdb::Pool::sequence_t SolverCore::getNextMissingBlock(const uint32_t starting_after) const
    {
        for(csdb::Pool::sequence_t b = starting_after + 1; b < cur_round; ++b) {
            if(outrunning_blocks.count(b) > 0) {
                continue;
            }
            return b;
        }
        return 0;
    }

    csdb::Pool::sequence_t SolverCore::getCountCahchedBlock(csdb::Pool::sequence_t starting_after, csdb::Pool::sequence_t end) const
    {
        if(outrunning_blocks.empty()) {
            return 0;
        }
        // it - "сквозной" итератор для двух блоков while()
        auto it = outrunning_blocks.cbegin();
        // skip outdated blocks if any
        while(it->first <= starting_after) {
            if(++it == outrunning_blocks.cend()) {
                return 0;
            }
        }
        // count useful cached blocks
        csdb::Pool::sequence_t cnt = 0;
        while(it->first <= end) {
            ++cnt;
            if(++it == outrunning_blocks.cend()) {
                break;
            }
        }
        return cnt;
    }

} // slv2
