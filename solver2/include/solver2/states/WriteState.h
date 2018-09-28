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

        // override DefaultStateBehavior to ignore blocks
        Result onBlock(SolverContext& /*context*/, csdb::Pool& /*pool*/, const PublicKey& /*sender*/) override
        {
            return Result::Ignore;
        }

        const char * name() const override
        {
            return "Write";
        }

    private:

        std::unique_ptr<Hash> pown;

    };

} // slv2
