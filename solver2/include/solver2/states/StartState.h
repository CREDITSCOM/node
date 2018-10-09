#pragma once
#include "HandleRTState.h"

namespace slv2
{
    /**
     * @class   StartState
     *
     * @brief   A start node state. Intended to handle first round. This class cannot be inherited. When on,
     *          invokes immediately further transition according to round table. Similar to RoundTableHandler,
     *          but selects on of Start***State
     *
     * @author  Alexander Avramenko
     * @date    09.10.2018
     *
     * @sa  T:HandleRTState  
     *
     * ### remarks  Aae, 30.09.2018.
     */

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
