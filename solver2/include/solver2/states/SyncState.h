#pragma once
#include "DefaultIgnore.h"

namespace slv2
{

    class SyncState final : public DefaultIgnore
    {
    public:

        ~SyncState() override
        {}

        Result onRoundTable(SolverContext& context, const uint32_t round) override;

        const char * getName() const override
        {
            return "Sync";
        }
    };

} // slv2
