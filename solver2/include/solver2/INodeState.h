#pragma once

#include "Result.h"
#include <lib/system/keys.hpp> // PublicKey, Hash
#include <cstdint>

namespace csdb
{
    class Pool;
    class PoolHash;
    class Transaction;
}
namespace cs
{
    class TransactionsPacket;
    struct HashVector;
    struct HashMatrix;
    struct StageOne;
    struct StageTwo;
    struct StageThree;
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

        virtual void on(SolverContext& context) = 0;

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

        virtual void off(SolverContext& context) = 0;

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

        virtual void expired(SolverContext& context) = 0;

        /**
         * @fn  virtual void INodeState::onRoundEnd(SolverContext& context, bool is_bigbang) = 0;
         *
         * @brief   Called on the current round end
         *
         * @author  aae
         * @date    01.10.2018
         *
         * @param [in,out]  context     The context.
         * @param           is_bigbang  True if round finished by bigbang, false if not (normal finish).
         */

        virtual void onRoundEnd(SolverContext& context, bool is_bigbang) = 0;

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

        virtual Result onRoundTable(SolverContext& context, const size_t round) = 0;

        /**
         * @fn  virtual Result INodeState::onBlock(SolverContext& context, csdb::Pool& pool, const cs::PublicKey& sender) = 0;
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
         */

        virtual Result onBlock(SolverContext& context, csdb::Pool& pool, const cs::PublicKey& sender) = 0;

        /**
         * @fn  virtual Result INodeState::onHash(SolverContext& context, const csdb::PoolHash & pool_hash, const cs::PublicKey& sender) = 0;
         *
         * @brief   Is called when new hash is received.
         *
         * @author  aae
         * @date    01.10.2018
         *
         * @param [in]  context     The core context.
         * @param       pool_hash   The hash received.
         * @param       sender      The sender of hash.
         *
         * @return  A Result of event: Finish - core has to make a transition, Ignore - continue to work
         *          in current state, Failed - error occurs.
         */

        virtual Result onHash(SolverContext& context, const csdb::PoolHash & pool_hash, const cs::PublicKey& sender) = 0;

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
         * @fn  virtual Result INodeState::onSyncTransactions(SolverContext& context, cs::RoundNumber round) = 0;
         *
         * @brief   Is called when new transaction list is received. The SolverCore always updates own
         *          vector before call this method.
         *
         * @author  aae
         * @date    01.10.2018
         *
         * @param [in]  context The core context.
         * @param       round   The round.
         *
         * @return  A Result of event: Finish - core has to make a transition, Ignore - continue to work
         *          in current state, Failed - error occurs.
         */

        virtual Result onSyncTransactions(SolverContext& context, cs::RoundNumber round) = 0;

        // Solver3 new methods
        
        virtual Result onStage1(SolverContext& context, const cs::StageOne& stage) = 0;
        virtual Result onStage2(SolverContext& context, const cs::StageTwo& stage) = 0;
        virtual Result onStage3(SolverContext& context, const cs::StageThree& stage) = 0;

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
