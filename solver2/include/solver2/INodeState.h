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

    /**
     * @class   INodeState
     *
     * @brief   An interface of a node state in terms of StateMachine pattern
     *
     * @author  aae
     * @date    01.10.2018
     */

    class INodeState
    {
    public:

        virtual ~INodeState()
        {}

        /**
         * @fn  virtual void INodeState::on(SolverContext& ) = 0;
         *
         * @brief   Called when state becomes an active
         *
         * @author  aae
         * @date    01.10.2018
         *
         * @param [in]  parameter1  The core context.
         *
         * ### remarks  Aae, 30.09.2018.
         */

        virtual void on(SolverContext& /*context*/) = 0;

        /**
         * @fn  virtual void INodeState::off(SolverContext& ) = 0;
         *
         * @brief   Called when state becomes an inactive
         *
         * @author  aae
         * @date    01.10.2018
         *
         * @param [in]  parameter1  The core context.
         *
         * ### remarks  Aae, 30.09.2018.
         */

        virtual void off(SolverContext& /*context*/) = 0;

        /**
         * @fn  virtual void INodeState::expired(SolverContext& ) = 0;
         *
         * @brief   Called when timeout of current state is being active expired
         *
         * @author  aae
         * @date    01.10.2018
         *
         * @param [in]  parameter1  The core context.
         *
         * ### remarks  Aae, 30.09.2018.
         */

        virtual void expired(SolverContext& /*context*/) = 0;

        /**
         * @fn  virtual void INodeState::onRoundEnd(SolverContext& context) = 0;
         *
         * @brief   Called on the current round end
         *
         * @author  aae
         * @date    01.10.2018
         *
         * @param [in,out]  context The context.
         */

        virtual void onRoundEnd(SolverContext& /*context*/) = 0;

        /**
         * @fn  virtual Result INodeState::onRoundTable(SolverContext& context, const uint32_t round) = 0;
         *
         * @brief   Is called when the new round table is received
         *
         * @author  aae
         * @date    01.10.2018
         *
         * @param [in]  context The core context.
         * @param       round   The new round number.
         *
         * @return  A Result of event: Finish - core has to make a transition, Ignore - continue to work
         *          in current state, Failed - error occurs.
         *
         * ### remarks  Aae, 30.09.2018.
         */

        virtual Result onRoundTable(SolverContext& context, const uint32_t round) = 0;

        /**
         * @fn  virtual Result INodeState::onBlock(SolverContext& context, csdb::Pool& pool, const PublicKey& sender) = 0;
         *
         * @brief   Is called when new block is received.
         *
         * @author  aae
         * @date    01.10.2018
         *
         * @param [in]      context The core context.
         * @param [in,out]  pool    The pool.
         * @param           sender  The sender of new block.
         *
         * @return  A Result of event: Finish - core has to make a transition, Ignore - continue to work
         *          in current state, Failed - error occurs.
         *
         * ### remarks  Aae, 30.09.2018.
         */

        virtual Result onBlock(SolverContext& context, csdb::Pool& pool, const PublicKey& sender) = 0;

        /**
         * @fn  virtual Result INodeState::onVector(SolverContext& context, const Credits::HashVector& vect, const PublicKey& sender) = 0;
         *
         * @brief   Is called when new vector is received.
         *
         * @author  aae
         * @date    01.10.2018
         *
         * @param [in]  context The core context.
         * @param       vect    The vector received.
         * @param       sender  The sender of vector.
         *
         * @return  A Result of event: Finish - core has to make a transition, Ignore - continue to work
         *          in current state, Failed - error occurs.
         *
         * ### remarks  Aae, 30.09.2018.
         */

        virtual Result onVector(SolverContext& context, const Credits::HashVector& vect, const PublicKey& sender) = 0;

        /**
         * @fn  virtual Result INodeState::onMatrix(SolverContext& context, const Credits::HashMatrix& matr, const PublicKey& sender) = 0;
         *
         * @brief   Is called when new matrix is received.
         *
         * @author  aae
         * @date    01.10.2018
         *
         * @param [in]  context The core context.
         * @param       matr    The matrix received.
         * @param       sender  The sender of matrix.
         *
         * @return  A Result.
         *
         * ### remarks  Aae, 30.09.2018.
         */

        virtual Result onMatrix(SolverContext& context, const Credits::HashMatrix& matr, const PublicKey& sender) = 0;

        /**
         * @fn  virtual Result INodeState::onHash(SolverContext& context, const Hash& hash, const PublicKey& sender) = 0;
         *
         * @brief   Is called when new hash is received.
         *
         * @author  aae
         * @date    01.10.2018
         *
         * @param [in]  context The core context.
         * @param       hash    The hash received.
         * @param       sender  The sender of hash.
         *
         * @return  A Result of event: Finish - core has to make a transition, Ignore - continue to work
         *          in current state, Failed - error occurs.
         *
         * ### remarks  Aae, 30.09.2018.
         */

        virtual Result onHash(SolverContext& context, const Hash& hash, const PublicKey& sender) = 0;

        /**
         * @fn  virtual Result INodeState::onTransaction(SolverContext& context, const csdb::Transaction& trans) = 0;
         *
         * @brief   Is called when new transaction is received.
         *
         * @author  aae
         * @date    01.10.2018
         *
         * @param [in]  context The core context.
         * @param       trans   The transaction received.
         *
         * @return  A Result of event: Finish - core has to make a transition, Ignore - continue to work
         *          in current state, Failed - error occurs.
         *
         * ### remarks  Aae, 30.09.2018.
         */

        virtual Result onTransaction(SolverContext& context, const csdb::Transaction& trans) = 0;

        /**
         * @fn  virtual Result INodeState::onTransactionList(SolverContext& context, const csdb::Pool& pool) = 0;
         *
         * @brief   Is called when new transaction list is received.
         *
         * @author  aae
         * @date    01.10.2018
         *
         * @param [in]  context The core context.
         * @param       pool    The pool of transaction list received.
         *
         * @return  A Result of event: Finish - core has to make a transition, Ignore - continue to work
         *          in current state, Failed - error occurs.
         *
         * ### remarks  Aae, 30.09.2018.
         */

        virtual Result onTransactionList(SolverContext& context, const csdb::Pool& pool) = 0;

        /**
         * @fn  virtual const char * INodeState::name() const = 0;
         *
         * @brief   Gets the name of state
         *
         * @author  aae
         * @date    01.10.2018
         *
         * @return  A pointer to a const char.
         *
         * ### remarks  Aae, 30.09.2018.
         */

        virtual const char * name() const = 0;
    };
} // slv2
