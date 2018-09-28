#include "SolverCore.h"
#include <Solver/Solver.hpp>
#include "../Node.h"
#include <Solver/Generals.hpp>

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
        // "autostart" in node environment
        if(is_finished()) {
            start();
        }
    }

    void SolverCore::addInitialBalance()
    {
        if(opt_is_proxy_v1 && pslv_v1) {
            pslv_v1->addInitialBalance();
        }
    }

    void SolverCore::setBigBangStatus(bool status)
    {
        if(opt_is_proxy_v1 && pslv_v1) {
            pslv_v1->setBigBangStatus(status);
            return;
        }

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
        if(stateCompleted(pstate->onTransaction(context, trans))) {
            handleTransitions(Event::Transactions);
        }
    }

    void SolverCore::gotTransactionList(csdb::Pool& pool)
    {
        if(opt_is_proxy_v1 && pslv_v1) {
            csdb::Pool tmp = pool;
            pslv_v1->gotTransactionList(std::move(tmp));
            return;
        }

        // чистим, если список не пуст, для нового списка
        if(m_pool.transactions_count() > 0) {
            m_pool = csdb::Pool {};
        }
        // bad tansactions storage:
        csdb::Pool b_pool {};
        // update own hash vector
        auto result = pgen->buildvector(pool, m_pool, pnode->getConfidants().size(), b_pool);
        pown_hvec->Sender = pnode->getMyConfNumber();
        pown_hvec->hash = result;

        if(!pstate) {
            return;
        }
        if(stateCompleted(pstate->onTransactionList(context, pool))) {
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
        if(stateCompleted(pstate->onVector(context, vect, PublicKey {}))) {
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
        if(stateCompleted(pstate->onMatrix(context, matr, PublicKey {}))) {
            handleTransitions(Event::Matrices);
        }
    }

    void SolverCore::gotBlock(csdb::Pool& pool, const PublicKey& sender)
    {
        if(opt_is_proxy_v1 && pslv_v1) {
            csdb::Pool tmp = pool;
            pslv_v1->gotBlock(std::move(tmp), sender);
            return;
        }

        if(!pstate) {
            return;
        }
        if(stateCompleted(pstate->onBlock(context, pool, sender))) {
            handleTransitions(Event::Block);
        }
    }

    void SolverCore::gotBlockRequest(const csdb::PoolHash& pool_hash, const PublicKey& sender)
    {
        if(opt_is_proxy_v1 && pslv_v1) {
            csdb::PoolHash tmp = pool_hash;
            pslv_v1->gotBlockRequest(std::move(tmp), sender);
            return;
        }
        // state does not take part
        csdb::Pool pool = pnode->getBlockChain().loadBlock(pool_hash);
        if(pool.is_valid())        {
            pool.set_previous_hash(csdb::PoolHash::from_string(""));
            pnode->sendBlockReply(std::move(pool), sender);
        }
    }

    void SolverCore::gotBlockReply(csdb::Pool& pool)
    {
        if(opt_is_proxy_v1 && pslv_v1) {
            pslv_v1->gotBlockReply(std::move(pool));
            return;
        }

        //std::cout << "Solver -> Got Block for my Request: " << pool.sequence() << std::endl;
        if(pool.sequence() == pnode->getBlockChain().getLastWrittenSequence() + 1) {
            pnode->getBlockChain().putBlock(pool);
        }
    }

    void SolverCore::gotHash(const Hash& hash, const PublicKey& sender)
    {
        if(opt_is_proxy_v1 && pslv_v1) {
            pslv_v1->gotHash(hash, sender);
            return;
        }

        if(!pstate) {
            return;
        }
        if(stateCompleted(pstate->onHash(context, hash, sender))) {
            handleTransitions(Event::Hashes);
        }
    }

    void SolverCore::addConfirmation(uint8_t own_conf_number)
    {
        if(opt_is_proxy_v1 && pslv_v1) {
            pslv_v1->addConfirmation(own_conf_number);
            return;
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
    }

    void SolverCore::nextRound()
    {
        if(opt_is_proxy_v1 && pslv_v1) {
            pslv_v1->nextRound();
            return;
        }

        //std::cout << "SOLVER> next Round : Starting ... nextRound" << std::endl;

        // as store result of current round:
        recv_vect.clear();
        recv_matr.clear();
        recv_hash.clear();

        if(!pstate) {
            return;
        }
        cur_round = pnode->getRoundNumber();
        if(stateCompleted(pstate->onRoundTable(context, cur_round))) {
            handleTransitions(Event::RoundTable);
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
        transactions.push_back(tr);
    }

} // slv2
