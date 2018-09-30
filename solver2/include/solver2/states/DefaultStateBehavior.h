#pragma once

#include "../INodeState.h"

namespace slv2
{
    /// <summary>   Implements a default node state behavior. Intended to be inherited by most of all final states classes </summary>
    ///
    /// <remarks>   Aae, 30.09.2018. </remarks>
    ///
    /// <seealso cref="T:INodeState"/>

    class DefaultStateBehavior : public INodeState
    {
    public:

        ~DefaultStateBehavior() override
        {}

        /// <summary>   Executes the round table action. Signals for core to make transition on Event::RoundTable </summary>
        ///
        /// <remarks>   Aae, 30.09.2018. </remarks>
        ///
        /// <param name="context">  [in,out] The context. </param>
        /// <param name="round">    The new round number. </param>
        ///
        /// <returns>   A Result::Finished value. </returns>

        Result onRoundTable(SolverContext& context, const uint32_t round) override;

        /// <summary>   Executes the receive of block. Has to be invoked from overrides if any. 
        ///             Performs default block handling suitable for almost any state</summary>
        ///
        /// <remarks>   Aae, 30.09.2018. </remarks>
        ///
        /// <param name="context">  [in,out] The context. </param>
        /// <param name="block">    [in,out] The block received </param>
        /// <param name="sender">   The sender of block </param>
        ///
        /// <returns>   A Result::Finish value</returns>

        Result onBlock(SolverContext& context, csdb::Pool& block, const PublicKey& sender) override;

        /// <summary>   Ignores vector received </summary>
        ///
        /// <remarks>   Aae, 30.09.2018. </remarks>
        ///
        /// <param name="context">  [in,out] The context. </param>
        /// <param name="vect">     The vect. </param>
        /// <param name="sender">   The sender. </param>
        ///
        /// <returns>   A Result::Ignore value </returns>

        Result onVector(SolverContext& context, const Credits::HashVector& vect, const PublicKey& sender) override;

        /// <summary>   Ignores the matrix received </summary>
        ///
        /// <remarks>   Aae, 30.09.2018. </remarks>
        ///
        /// <param name="context">  [in,out] The context. </param>
        /// <param name="matr">     The matr. </param>
        /// <param name="sender">   The sender. </param>
        ///
        /// <returns>   A Result::Ignore value </returns>

        Result onMatrix(SolverContext& context, const Credits::HashMatrix& matr, const PublicKey& sender) override;

        /// <summary>   Ignores the hash received </summary>
        ///
        /// <remarks>   Aae, 30.09.2018. </remarks>
        ///
        /// <param name="context">  [in,out] The context. </param>
        /// <param name="hash">     The hash. </param>
        /// <param name="sender">   The sender. </param>
        ///
        /// <returns>   A Result::Ignore value </returns>

        Result onHash(SolverContext& context, const Hash& hash, const PublicKey& sender) override;

        /// <summary>   Ignores the transaction received </summary>
        ///
        /// <remarks>   Aae, 30.09.2018. </remarks>
        ///
        /// <param name="context">  [in,out] The context. </param>
        /// <param name="trans">    The transaction. </param>
        ///
        /// <returns>   A Result::Ignore value </returns>

        Result onTransaction(SolverContext& context, const csdb::Transaction& trans) override;

        /// <summary>   Ignores the transaction list received </summary>
        ///
        /// <remarks>   Aae, 30.09.2018. </remarks>
        ///
        /// <param name="context">  [in,out] The context. </param>
        /// <param name="pool">     The pool. </param>
        ///
        /// <returns>   A Result::Ignore value </returns>

        Result onTransactionList(SolverContext& context, const csdb::Pool& pool) override;

    };

} // slv2
