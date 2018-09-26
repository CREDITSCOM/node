#include "CollectState.h"
#include "../SolverCore.h"
#if defined(SOLVER_USES_PROXY_TYPES)
#include "ProxyTypes.h"
#else
#include <csdb/pool.h>
#endif
#include <iostream>

namespace slv2
{
    void CollectState::stateOn(SolverContext& /*context*/)
    {
        std::cout << getName() << ": starting to collect transactions" << std::endl;
        m_cntTransactions = 0;
    }

    void CollectState::stateOff(SolverContext& /*context*/)
    {
        std::cout << getName() << ": " << m_cntTransactions << " are collected" << std::endl;
    }

    Result CollectState::onRoundTable(SolverContext& /*context*/, const uint32_t round)
    {
        std::cout << getName() << ": round table received: " << round << std::endl;
        return Result::Finish;
    }

    Result CollectState::onTransaction(SolverContext& /*context*/, const csdb::Transaction & /*trans*/)
    {
        std::cout << getName() << ": transaction accepted (1)" << std::endl;
        ++m_cntTransactions;
        return Result::Ignore;
    }

    Result CollectState::onTransactionList(SolverContext& /*context*/, const csdb::Pool& pool)
    {
        std::cout << getName() << ": transaction list accepted (" << pool.transactions_count() << ")" << std::endl;
        m_cntTransactions += pool.transactions_count();
        return Result::Ignore;
    }


} // slv2
