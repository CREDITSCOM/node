#include "DefaultIgnore.h"

#if defined(SOLVER_USES_PROXY_TYPES)
#include "ProxyTypes.h"
#else
#include <csdb/pool.h>
#endif

#include <iostream>

namespace slv2
{

    Result DefaultIgnore::onRoundTable(SolverContext& /*context*/, const uint32_t round)
    {
        std::cout << name() << ": round table ignored: " << round << std::endl;
        return Result::Ignore;
    }

    Result DefaultIgnore::onBlock(SolverContext& /*context*/, const csdb::Pool& pool, const PublicKey& /*sender*/)
    {
        std::cout << name() << ": block ignored (#" << pool.sequence() << " of " << pool.transactions_count() << " transactions)" << std::endl;
        return Result::Ignore;
    }

    Result DefaultIgnore::onVector(SolverContext& /*context*/, const Credits::HashVector& /*vect*/, const PublicKey& /*sender*/)
    {
        std::cout << name() << ": vector ignored" << std::endl;
        return Result::Ignore;
    }

    Result DefaultIgnore::onMatrix(SolverContext& /*context*/, const Credits::HashMatrix& /*matr*/, const PublicKey& /*sender*/)
    {
        std::cout << name() << ": matrix ignored" << std::endl;
        return Result::Ignore;
    }

    Result DefaultIgnore::onHash(SolverContext& /*context*/, const Hash& /*hash*/, const PublicKey& /*sender*/)
    {
        std::cout << name() << ": hash ignored" << std::endl;
        return Result::Ignore;
    }

    Result DefaultIgnore::onTransaction(SolverContext& /*context*/, const csdb::Transaction& /*trans*/)
    {
        std::cout << name() << ": transaction ignored" << std::endl;
        return Result::Ignore;
    }

    Result DefaultIgnore::onTransactionList(SolverContext& /*context*/, const csdb::Pool& /*pool*/)
    {
        std::cout << name() << ": transaction list ignored" << std::endl;
        return Result::Ignore;
    }

} // slv2
