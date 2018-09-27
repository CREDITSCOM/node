#include "SolverCore.h"
#include <Solver/Solver.hpp>

namespace slv2
{

    const Credits::HashVector& SolverCore::getMyVector() const
    {
        if(pslv_v1) {
            return pslv_v1->getMyVector();
        }
        static Credits::HashVector stub{};
        return stub;
    }

    const Credits::HashMatrix& SolverCore::getMyMatrix() const
    {
        if(pslv_v1) {
            return pslv_v1->getMyMatrix();
        }
        static Credits::HashMatrix stub {};
        return stub;
    }

    void SolverCore::set_keys(const csdb::internal::byte_array& pub, const csdb::internal::byte_array& priv)
    {
        if(pslv_v1) {
            pslv_v1->set_keys(pub, priv);
        }
        //
        public_key = pub;
        private_key = priv;

        // "autostart" in node environment
        if(is_finished()) {
            start();
        }
    }

    void SolverCore::addInitialBalance()
    {
        if(pslv_v1) {
            pslv_v1->addInitialBalance();
        }
    }

    void SolverCore::setBigBangStatus(bool status)
    {
        if(pslv_v1) {
            pslv_v1->setBigBangStatus(status);
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
        if(pslv_v1) {
            csdb::Transaction tmp = trans;
            pslv_v1->gotTransaction(std::move(tmp));
        }

        if(!pstate) {
            return;
        }
        if(stateCompleted(pstate->onTransaction(context, trans))) {
            handleTransitions(Event::Transactions);
        }
    }

    void SolverCore::gotTransactionList(const csdb::Pool& pool)
    {
        if(pslv_v1) {
            csdb::Pool tmp = pool;
            pslv_v1->gotTransactionList(std::move(tmp));
        }

        if(!pstate) {
            return;
        }
        if(stateCompleted(pstate->onTransactionList(context, pool))) {
            handleTransitions(Event::Transactions);
        }
    }

    void SolverCore::gotVector(const Credits::HashVector& vect)
    {
        if(pslv_v1) {
            Credits::HashVector tmp = vect;
            pslv_v1->gotVector(std::move(tmp));
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
        if(pslv_v1) {
            Credits::HashMatrix tmp = matr;
            pslv_v1->gotMatrix(std::move(tmp));
        }

        if(!pstate) {
            return;
        }
        //TODO: how to get real public key from vect.Sender?
        if(stateCompleted(pstate->onMatrix(context, matr, PublicKey {}))) {
            handleTransitions(Event::Matrices);
        }
    }

    void SolverCore::gotBlock(const csdb::Pool& pool, const PublicKey& sender)
    {
        if(pslv_v1) {
            csdb::Pool tmp = pool;
            pslv_v1->gotBlock(std::move(tmp), sender);
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
        if(pslv_v1) {
            csdb::PoolHash tmp = pool_hash;
            pslv_v1->gotBlockRequest(std::move(tmp), sender);
        }

        if(!pstate) {
            return;
        }
    }

    void SolverCore::gotBlockReply(const csdb::Pool& pool)
    {
        if(pslv_v1) {
            csdb::Pool tmp = pool;
            pslv_v1->gotBlockReply(std::move(tmp));
        }

        if(!pstate) {
            return;
        }

    }

    void SolverCore::gotHash(const Hash& hash, const PublicKey& sender)
    {
        if(pslv_v1) {
            pslv_v1->gotHash(hash, sender);
        }

        if(!pstate) {
            return;
        }
        if(stateCompleted(pstate->onHash(context, hash, sender))) {
            handleTransitions(Event::Hashes);
        }
    }

    void SolverCore::addConfirmation(uint8_t conf_number)
    {
        if(pslv_v1) {
            pslv_v1->addConfirmation(conf_number);
        }

        if(!pstate) {
            return;
        }
    }

    void SolverCore::beforeNextRound()
    {
        if(pslv_v1) {
            pslv_v1->beforeNextRound();
        }
        
        if(!pstate) {
            return;
        }
    }

    void SolverCore::nextRound()
    {
        if(pslv_v1) {
            pslv_v1->nextRound();
        }

        if(!pstate) {
            return;
        }
        //TODO: get round number from node_
        ++cur_round;
        if(stateCompleted(pstate->onRoundTable(context, cur_round))) {
            handleTransitions(Event::RoundTable);
        }
    }

    void SolverCore::send_wallet_transaction(const csdb::Transaction& trans)
    {
        if(pslv_v1) {
            pslv_v1->send_wallet_transaction(trans);
        }
    }

} // slv2
