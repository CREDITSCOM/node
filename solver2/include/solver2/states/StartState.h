#pragma once
#include "HandleRTState.h"

namespace slv2
{
    /// <summary>   A start node state. Intended to handle first round. This class cannot be inherited. </summary>
    ///
    /// <remarks>   Aae, 30.09.2018. </remarks>
    ///
    /// <seealso cref="T:DefaultStateBehavior"/>

    class StartState final : public HandleRTState
    {
    public:

        ~StartState() override
        {}

        const char * name() const override
        {
            return "Start";
        }

    };

} // slv2
