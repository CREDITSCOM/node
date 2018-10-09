#pragma once
#include "DefaultStateBehavior.h"

namespace slv2
{
    /**
     * @class   HandleRTState
     *
     * @brief   A special state to handle a new round table. This class cannot be inherited. When on,
     *          invokes immediately further transition according to round table
     *
     * @author  Alexander Avramenko
     * @date    09.10.2018
     *
     * @sa  T:DefaultStateBehavior  
     *
     * ### remarks  Aae, 30.09.2018.
     */

    class HandleRTState : public DefaultStateBehavior
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
