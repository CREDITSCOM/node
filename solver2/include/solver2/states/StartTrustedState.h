#pragma once

#include "TrustedState.h"

namespace slv2
{
    /**
     * @class   StartTrustedState
     *
     * @brief   A start trusted state. To be activated mostly on the 1st round if node level is
     *          NodeLevel::Confidant. Currently acts as TrustedState exactly
     *
     * @author  Alexander Avramenko
     * @date    09.10.2018
     *
     * @sa  T:TrustedState
     */

    class StartTrustedState final : public TrustedState
    {
    public:

        ~StartTrustedState()
        {}

        const char * name() const override
        {
            return "StartTrusted";
        }

    };
} // slv2
