#pragma once
#include "Result.h"

#if defined(SOLVER_USES_PROXY_TYPES)
class PublicKey;
class Hash;
#else
#include <lib/system/keys.hpp> // Hash, PublicKey
#endif

#include <cstdint>

namespace csdb
{
    class Pool;
    class Transaction;
}
namespace Credits
{
    struct HashVector;
    struct HashMatrix;
}

namespace slv2
{
    class SolverContext;

    class INodeState
    {
    public:

        virtual ~INodeState()
        {}

        virtual void stateOn(SolverContext& /*context*/)
        {}

        virtual void stateOff(SolverContext& /*context*/)
        {}

        virtual void stateExpired(SolverContext& /*context*/)
        {}

        virtual Result onRoundTable(SolverContext& context, const uint32_t round) = 0;

        virtual Result onBlock(SolverContext& context, const csdb::Pool& pool, const PublicKey& sender) = 0;

        virtual Result onVector(SolverContext& context, const Credits::HashVector& vect, const PublicKey& sender) = 0;

        virtual Result onMatrix(SolverContext& context, const Credits::HashMatrix& matr, const PublicKey& sender) = 0;

        virtual Result onHash(SolverContext& context, const Hash& hash, const PublicKey& sender) = 0;

        virtual Result onTransaction(SolverContext& context, const csdb::Transaction& trans) = 0;

        virtual Result onTransactionList(SolverContext& context, const csdb::Pool& pool) = 0;

        virtual const char * getName() const = 0;
    };
} // slv2
