#pragma once

#include "NormalState.h"

namespace slv2
{

    class StartNormalState final : public NormalState
    {
    public:

        virtual ~StartNormalState()
        {}

        virtual const char * name() const
        {
            return "StartNormal";
        }
    };

} // slv2
