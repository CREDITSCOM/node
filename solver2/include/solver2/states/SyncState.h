#pragma once
#include "DefaultStateBehavior.h"

namespace slv2
{

    class SyncState final : public DefaultStateBehavior
    {
    public:

        ~SyncState() override
        {}

        const char * name() const override
        {
            return "Sync";
        }
    };

} // slv2
