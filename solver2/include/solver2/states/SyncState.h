#pragma once
#include "DefaultStateBehavior.h"

namespace slv2
{
    /// <summary>   A synchronise state. Used for node normal mode when some blocks are missed. This class cannot be inherited. </summary>
    ///
    /// <remarks>   Aae, 30.09.2018. </remarks>
    ///
    /// <seealso cref="T:DefaultStateBehavior"/>

    class SyncState final : public DefaultStateBehavior
    {
    public:

        ~SyncState() override
        {}

        void on(SolverContext& context) override;

        const char * name() const override
        {
            return "Sync";
        }
    };

} // slv2
