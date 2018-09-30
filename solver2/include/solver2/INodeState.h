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

    /// <summary>   An interface of a node state in terms of StateMachine pattern
    class INodeState
    {
    public:

        virtual ~INodeState()
        {}

        /// <summary>   Is called when state becomes an active </summary>
        ///
        /// <remarks>   Aae, 30.09.2018. </remarks>
        ///
        /// <param name="parameter1">   [in] The core context </param>

        virtual void on(SolverContext& /*context*/)
        {}

        /// <summary>   Is called when state becomes an inactive </summary>
        ///
        /// <remarks>   Aae, 30.09.2018. </remarks>
        ///
        /// <param name="parameter1">   [in] The core context </param>

        virtual void off(SolverContext& /*context*/)
        {}

        /// <summary>   Is called when timeout of current state is being active expired </summary>
        ///
        /// <remarks>   Aae, 30.09.2018. </remarks>
        ///
        /// <param name="parameter1">   [in] The core context </param>

        virtual void expired(SolverContext& /*context*/)
        {}

        /// <summary>   Is called when the new round table is received </summary>
        ///
        /// <remarks>   Aae, 30.09.2018. </remarks>
        ///
        /// <param name="context">  [in] The core context. </param>
        /// <param name="round">    The new round number. </param>
        ///
        /// <returns>   A Result of event: Finish - core has to make a transition, Ignore - continue to work in current state, Failed - error occurs </returns>

        virtual Result onRoundTable(SolverContext& context, const uint32_t round) = 0;

        /// <summary>   Is called when new block is received. </summary>
        ///
        /// <remarks>   Aae, 30.09.2018. </remarks>
        ///
        /// <param name="context">  [in] The core context. </param>
        /// <param name="pool">     [in,out] The pool. </param>
        /// <param name="sender">   The sender of new block. </param>
        ///
        /// <returns>   A Result of event: Finish - core has to make a transition, Ignore - continue to work in current state, Failed - error occurs </returns>

        virtual Result onBlock(SolverContext& context, csdb::Pool& pool, const PublicKey& sender) = 0;

        /// <summary>   Is called when new vector is received. </summary>
        ///
        /// <remarks>   Aae, 30.09.2018. </remarks>
        ///
        /// <param name="context">  [in] The core context. </param>
        /// <param name="vect">     The vector received </param>
        /// <param name="sender">   The sender of vector </param>
        ///
        /// <returns>   A Result of event: Finish - core has to make a transition, Ignore - continue to work in current state, Failed - error occurs </returns>

        virtual Result onVector(SolverContext& context, const Credits::HashVector& vect, const PublicKey& sender) = 0;

        /// <summary>   Is called when new matrix is received. </summary>
        ///
        /// <remarks>   Aae, 30.09.2018. </remarks>
        ///
        /// <param name="context">  [in] The core context. </param>
        /// <param name="matr">     The matrix received </param>
        /// <param name="sender">   The sender of matrix </param>
        ///
        /// <returns>   A Result. </returns>

        virtual Result onMatrix(SolverContext& context, const Credits::HashMatrix& matr, const PublicKey& sender) = 0;

        /// <summary>   Is called when new hash is received. </summary>
        ///
        /// <remarks>   Aae, 30.09.2018. </remarks>
        ///
        /// <param name="context">  [in] The core context. </param>
        /// <param name="hash">     The hash received. </param>
        /// <param name="sender">   The sender of hash </param>
        ///
        /// <returns>   A Result of event: Finish - core has to make a transition, Ignore - continue to work in current state, Failed - error occurs </returns>

        virtual Result onHash(SolverContext& context, const Hash& hash, const PublicKey& sender) = 0;

        /// <summary>   Is called when new transaction is received. </summary>
        ///
        /// <remarks>   Aae, 30.09.2018. </remarks>
        ///
        /// <param name="context">  [in] The core context. </param>
        /// <param name="trans">    The transaction received </param>
        ///
        /// <returns>   A Result of event: Finish - core has to make a transition, Ignore - continue to work in current state, Failed - error occurs </returns>

        virtual Result onTransaction(SolverContext& context, const csdb::Transaction& trans) = 0;

        /// <summary>   Is called when new transaction list is received. </summary>
        ///
        /// <remarks>   Aae, 30.09.2018. </remarks>
        ///
        /// <param name="context">  [in] The core context. </param>
        /// <param name="pool">     The pool of transaction list received </param>
        ///
        /// <returns>   A Result of event: Finish - core has to make a transition, Ignore - continue to work in current state, Failed - error occurs </returns>

        virtual Result onTransactionList(SolverContext& context, const csdb::Pool& pool) = 0;

        /// <summary>   Gets the name of state </summary>
        ///
        /// <remarks>   Aae, 30.09.2018. </remarks>
        ///
        /// <returns>   A pointer to a const char. </returns>

        virtual const char * name() const = 0;
    };
} // slv2
