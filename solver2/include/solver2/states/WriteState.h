#pragma once
#include "DefaultStateBehavior.h"
#include <memory>

namespace slv2
{
    /**
     * @class   WriteState
     *
     * @brief   A write node state. This class cannot be inherited.
     *
     * @author  aae
     * @date    02.10.2018
     *
     * @sa  T:DefaultStateBehavior  
     *
     * ### remarks  Aae, 30.09.2018.
     */

    class WriteState final : public DefaultStateBehavior
    {
    public:

        ~WriteState() override
        {}

        void on(SolverContext& context) override;

        /**
         * @fn  Result final::onHash(SolverContext& context, const Hash& hash, const PublicKey& sender) override;
         *
         * @brief   Executes the hash action
         *
         * @author  aae
         * @date    02.10.2018
         *
         * @param [in,out]  context The context.
         * @param           hash    The hash.
         * @param           sender  The sender.
         *
         * @return  A Result.
         */

        Result onHash(SolverContext& context, const Hash& hash, const PublicKey& sender) override;

        /**
         * @fn  Result final::onBlock(SolverContext& , csdb::Pool& , const PublicKey& ) override
         *
         * @brief   Override DefaultStateBehavior's method to ignore blocks received
         *
         * @author  aae
         * @date    02.10.2018
         *
         * @param [in,out]  parameter1  The first parameter.
         * @param [in,out]  parameter2  The second parameter.
         * @param           parameter3  The third parameter.
         *
         * @return  A Result::Ignore value.
         *
         * ### remarks  Aae, 30.09.2018.
         * ### param            context not used.
         * ### param            pool    not used.
         * ### param            sender  not used.
         */

        Result onBlock(SolverContext& /*context*/, csdb::Pool& /*pool*/, const PublicKey& /*sender*/) override
        {
            return Result::Ignore;
        }

        const char * name() const override
        {
            return "Write";
        }

    private:

        /** @brief   The pointer to own hash actual this round */
        std::unique_ptr<Hash> pown;

    };

} // slv2
