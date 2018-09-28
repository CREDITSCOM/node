#pragma once
#include "DefaultStateBehavior.h"
#include <memory>

namespace slv2
{

    class WriteState final : public DefaultStateBehavior
    {
    public:

        ~WriteState() override
        {}

        void on(SolverContext& context) override;

        Result onHash(SolverContext& context, const Hash& hash, const PublicKey& sender) override;

        const char * name() const override
        {
            return "Write";
        }

    private:

        std::unique_ptr<Hash> pown;

    };

} // slv2
