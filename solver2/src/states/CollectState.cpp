#include "CollectState.h"
#include "../SolverCore.h"
#include "../Node.h"

#include <iostream>

namespace slv2
{
    void CollectState::on(SolverContext& /*context*/)
    {
        // таким способом v_pool, m_pool обнул€ютс€ в solver.v1
        pool = csdb::Pool {};
        std::cout << name() << ": starting to collect transactions" << std::endl;
    }

    void CollectState::off(SolverContext& context)
    {
        std::cout << name() << ": " << pool.transactions_count() << " are collected" << std::endl;
        pool.set_sequence(context.round());
        context.node().sendTransactionList(std::move(pool)); // Correct sending, better when to all one time
    }

    Result CollectState::onRoundTable(SolverContext& /*context*/, const uint32_t round)
    {
        std::cout << name() << ": round table received: " << round << std::endl;
        return Result::Finish;
    }

    Result CollectState::onTransaction(SolverContext& context, const csdb::Transaction & tr)
    {
        if(tr.is_valid())
        {
            if(!context.is_spammer()) {
                if(context.verify(tr)) {
                    pool.add_transaction(tr);
                }
                else {
                    std::cout << name() << ": wrong signature" << std::endl;
                }
            }
            else {
                pool.add_transaction(tr);
                std::cout << name() << ": transaction accepted (1)" << std::endl;
            }
        }
        else {
            std::cout << name() << ": invalid transaction received" << std::endl;
        }

        ++cnt_transactions;
        return Result::Ignore;
    }

    Result CollectState::onTransactionList(SolverContext& /*context*/, const csdb::Pool& tl)
    {
        std::cout << name() << ": transaction list accepted (" << tl.transactions_count() << ")" << std::endl;
        cnt_transactions += pool.transactions_count();
        return Result::Ignore;
    }


} // slv2
