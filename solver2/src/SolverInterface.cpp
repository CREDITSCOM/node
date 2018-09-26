#include "SolverCore.h"
#include <Solver/Solver.hpp>

namespace slv2
{

    const Credits::HashVector& SolverCore::getMyVector() const
    {
        if(m_pSolvV1) {
            return m_pSolvV1->getMyVector();
        }
        static Credits::HashVector stub{};
        return stub;
    }

    const Credits::HashMatrix& SolverCore::getMyMatrix() const
    {
        if(m_pSolvV1) {
            return m_pSolvV1->getMyMatrix();
        }
        static Credits::HashMatrix stub {};
        return stub;
    }

    void SolverCore::set_keys(const csdb::internal::byte_array& /*pub*/, const csdb::internal::byte_array& /*priv*/)
    {
    }

    void SolverCore::addInitialBalance()
    {
    }

    void SolverCore::setBigBangStatus(bool status)
    {
        if(!m_pState) {
            return;
        }
        if(status) {
            handleTransitions(Event::BigBang);
        }
    }

    void SolverCore::gotTransaction(const csdb::Transaction& trans)
    {
        if(!m_pState) {
            return;
        }
        if(stateCompleted(m_pState->onTransaction(m_context, trans))) {
            handleTransitions(Event::Transactions);
        }
    }

    void SolverCore::gotTransactionList(const csdb::Pool& pool)
    {
        if(!m_pState) {
            return;
        }
        if(stateCompleted(m_pState->onTransactionList(m_context, pool))) {
            handleTransitions(Event::Transactions);
        }
    }

    void SolverCore::gotVector(const Credits::HashVector& vect)
    {
        if(!m_pState) {
            return;
        }
        //TODO: how to get real public key from vect.Sender?
        if(stateCompleted(m_pState->onVector(m_context, vect, PublicKey {}))) {
            handleTransitions(Event::Vectors);
        }
    }

    void SolverCore::gotMatrix(const Credits::HashMatrix& matr)
    {
        if(!m_pState) {
            return;
        }
        //TODO: how to get real public key from vect.Sender?
        if(stateCompleted(m_pState->onMatrix(m_context, matr, PublicKey {}))) {
            handleTransitions(Event::Matrices);
        }
    }

    void SolverCore::gotBlock(const csdb::Pool& pool, const PublicKey& sender)
    {
        if(!m_pState) {
            return;
        }
        if(stateCompleted(m_pState->onBlock(m_context, pool, sender))) {
            handleTransitions(Event::Block);
        }
    }

    void SolverCore::gotBlockRequest(const csdb::PoolHash& /*pool_hash*/, const PublicKey& /*sender*/)
    {
        if(!m_pState) {
            return;
        }

    }

    void SolverCore::gotBlockReply(const csdb::Pool& /*pool*/)
    {
        if(!m_pState) {
            return;
        }

    }

    void SolverCore::gotHash(const Hash& hash, const PublicKey& sender)
    {
        if(!m_pState) {
            return;
        }
        if(stateCompleted(m_pState->onHash(m_context, hash, sender))) {
            handleTransitions(Event::Hashes);
        }
    }

    void SolverCore::addConfirmation(uint8_t /*conf_number*/)
    {
        if(!m_pState) {
            return;
        }

    }

    void SolverCore::beforeNextRound()
    {
        if(!m_pState) {
            return;
        }

    }

    void SolverCore::nextRound()
    {
        if(!m_pState) {
            return;
        }
        //TODO: get round number from node_
        ++m_round;
        if(stateCompleted(m_pState->onRoundTable(m_context, m_round))) {
            handleTransitions(Event::RoundTable);
        }
    }

} // slv2
