#pragma once

#include "CollectState.h"

namespace slv2
{

    class StartCollectState final: public CollectState
    {
    public:

        virtual ~StartCollectState()
        {}

        virtual const char * name() const
        {
            return "StartCollect";
        }

    };

} // slv2
