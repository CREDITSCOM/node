#pragma once
#include "DefaultStateBehavior.h"
#include <memory>

namespace slv2
{
    /// <summary>   A write node state. This class cannot be inherited. </summary>
    ///
    /// <remarks>   Aae, 30.09.2018. </remarks>
    ///
    /// <seealso cref="T:DefaultStateBehavior"/>

    class WriteState final : public DefaultStateBehavior
    {
    public:

        ~WriteState() override
        {}

        void on(SolverContext& context) override;

        Result onHash(SolverContext& context, const Hash& hash, const PublicKey& sender) override;

        /// <summary>   Override DefaultStateBehavior's method to ignore blocks received </summary>
        ///
        /// <remarks>   Aae, 30.09.2018. </remarks>
        ///
        /// <param name="context">  not used. </param>
        /// <param name="pool">     not used. </param>
        /// <param name="sender">   not used. </param>
        ///
        /// <returns>   A Result::Ignore value </returns>

        Result onBlock(SolverContext& /*context*/, csdb::Pool& /*pool*/, const PublicKey& /*sender*/) override
        {
            return Result::Ignore;
        }

        const char * name() const override
        {
            return "Write";
        }

    private:

        /// <summary>   The pointer to own hash actual this round </summary>
        std::unique_ptr<Hash> pown;

    };

} // slv2
