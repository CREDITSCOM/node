#pragma once
#include "DefaultStateBehavior.h"

#if defined(SOLVER_USES_PROXY_TYPES)
#include "ProxyTypes.h"
#else
#include <csdb/pool.h>
#endif

namespace slv2
{
    /// <summary>   A transaction collector node state (so called "main node"). This class cannot be inherited. </summary>
    ///
    /// <remarks>   Aae, 30.09.2018. </remarks>
    ///
    /// <seealso cref="T:DefaultStateBehavior"/>

    class CollectState final : public DefaultStateBehavior
    {
    public:

        ~CollectState() override
        {}

        void on(SolverContext& context) override;

        void off(SolverContext& context) override;

        Result onTransaction(SolverContext& context, const csdb::Transaction& tr) override;

        Result onTransactionList(SolverContext& context, const csdb::Pool& pool) override;

        const char * name() const override
        {
            return "Collect";
        }

    private:

        size_t cnt_transactions { 0 };
        csdb::Pool pool;
    };

} // slv2
