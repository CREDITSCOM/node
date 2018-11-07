#pragma once
#include "DefaultStateBehavior.h"
#include <CallsQueueScheduler.h>
#include <vector>

namespace slv2
{
    /**
     * @class   PermanentCollectWriteState
     *
     * @brief   A permanent collect write state. This class cannot be inherited. It is used in special testing mode to serve at the same time
     *          as Collector (main node) and Writer (write node) during the same round
     *
     * @author  Alexander Avramenko
     * @date    11.10.2018
     */

    class PrimitiveWriteState final : public DefaultStateBehavior
    {
    public:

        ~PrimitiveWriteState() override
        {}

        void on(SolverContext& context) override;

        void off(SolverContext& context) override;

        Result onHash(SolverContext& context, const Hash& hash, const PublicKey& sender) override;

        Result onTransactionList(SolverContext& context, csdb::Pool& pool) override;

        const char * name() const override
        {
            return "Primitive Write";
        }

    private:

        CallsQueueScheduler::CallTag tag_timeout { CallsQueueScheduler::no_tag };

        std::vector<PublicKey> trusted_candidates;
    };

} // slv2
