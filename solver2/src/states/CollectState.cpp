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
    void CollectState::on(SolverContext& /*context*/)
    {
        std::cout << name() << ": starting to collect transactions" << std::endl;
        cnt_transactions = 0;
    }

    void CollectState::off(SolverContext& /*context*/)
    {
        std::cout << name() << ": " << cnt_transactions << " are collected" << std::endl;
    }

    Result CollectState::onRoundTable(SolverContext& /*context*/, const uint32_t round)
    {
        std::cout << name() << ": round table received: " << round << std::endl;
        return Result::Finish;
    }

    Result CollectState::onTransaction(SolverContext& /*context*/, const csdb::Transaction & /*trans*/)
    {
        std::cout << name() << ": transaction accepted (1)" << std::endl;
        ++cnt_transactions;
        return Result::Ignore;
    }

    Result CollectState::onTransactionList(SolverContext& /*context*/, const csdb::Pool& pool)
    {
        std::cout << name() << ": transaction list accepted (" << pool.transactions_count() << ")" << std::endl;
        cnt_transactions += pool.transactions_count();
        return Result::Ignore;
    }


} // slv2
