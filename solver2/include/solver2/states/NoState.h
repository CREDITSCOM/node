#pragma once
#include "INodeState.h"

#include <lib/system/keys.hpp>

namespace slv2
{
    /**
     * @class   NoState
     *
     * @brief   A special state of "No state". Used at the very beginning of work. This class cannot
     *          be inherited. Does nothing when activated
     *
     * @author  Alexander Avramenko
     * @date    09.10.2018
     *
     * @sa  T:INodeState    
     *
     * ### remarks  Aae, 30.09.2018.
     */

    class NoState final : public INodeState
    {
    public:

        void on(SolverContext& /*context*/) override
        {}

        void off(SolverContext& /*context*/) override
        {}

        void expired(SolverContext& /*context*/) override
        {}

        void onRoundEnd(SolverContext& /*context*/) override
        {}

        /**
         * @fn  Result final::onRoundTable(SolverContext& , const uint32_t ) override
         *
         * @brief   Handles the round table action
         *
         * @author  Alexander Avramenko
         * @date    09.10.2018
         *
         * @param [in,out]  parameter1  The first parameter.
         * @param           uint32_t    The 32 t.
         *
         * @return  A Result::Finish to allow/initiate transition to proper state.
         */

        Result onRoundTable(SolverContext& /*context*/, const uint32_t /*round*/) override
        {
            return Result::Finish;
        }

        Result onBlock(SolverContext& /*context*/, csdb::Pool& /*pool*/, const cs::PublicKey& /*sender*/) override
        {
            return Result::Failure;
        }

        Result onVector(SolverContext& /*context*/, const cs::HashVector& /*vect*/, const cs::PublicKey& /*sender*/) override
        {
            return Result::Failure;
        }

        Result onMatrix(SolverContext& /*context*/, const cs::HashMatrix& /*matr*/, const cs::PublicKey& /*sender*/) override
        {
            return Result::Failure;
        }

        Result onHash(SolverContext& /*context*/, const cs::Hash& /*hash*/, const cs::PublicKey& /*sender*/) override
        {
            return Result::Failure;
        }

        Result onTransaction(SolverContext& /*context*/, const csdb::Transaction& /*trans*/) override
        {
            return Result::Failure;
        }

        Result onTransactionList(SolverContext& /*context*/, const csdb::Pool& /*pool*/) override
        {
            return Result::Failure;
        }

        const char * name() const override
        {
            return "None";
        }

    };

} // slv2
