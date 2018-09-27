#pragma once
#include "DefaultIgnore.h"

namespace slv2
{

    class WriteState final : public DefaultIgnore
    {
    public:

        ~WriteState() override
        {}

        void on(SolverContext& context) override;

        Result onRoundTable(SolverContext& context, const uint32_t round) override;

        Result onHash(SolverContext& context, const Hash& hash, const PublicKey& sender) override;

        const char * name() const override
        {
            return "Write";
        }

    private:

        unsigned int cnt_hashes { 0 };
    };

} // slv2
