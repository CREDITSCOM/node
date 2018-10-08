#pragma once

#include "TrustedState.h"

namespace slv2
{

    class StartTrustedState : public TrustedState
    {
    public:

        virtual ~StartTrustedState()
        {}

        virtual const char * name() const
        {
            return "StartTrusted";
        }

    };
} // slv2
