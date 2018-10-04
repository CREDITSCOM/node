#pragma once
#include "DefaultStateBehavior.h"

namespace slv2
{
    /// <summary>   A special state to handle a new round table. This class cannot be inherited.
    ///             Invoke an immediate further transition according to round table</summary>
    ///
    /// <remarks>   Aae, 30.09.2018. </remarks>
    ///
    /// <seealso cref="T:DefaultStateBehavior"/>

    class HandleRTState final : public DefaultStateBehavior
    {
    public:

        ~HandleRTState() override
        {}

        void on(SolverContext& context) override;

        const char * name() const override
        {
            return "Handle RT";
        }
    };

} // slv2
