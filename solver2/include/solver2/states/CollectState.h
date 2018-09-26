#pragma once
#include "DefaultIgnore.h"

namespace slv2
{

    class CollectState final : public DefaultIgnore
    {
    public:

        ~CollectState() override
        {}

        void stateOn(SolverContext& context) override;

        void stateOff(SolverContext& context) override;

        Result onRoundTable(SolverContext& context, const uint32_t round) override;

        Result onTransaction(SolverContext& context, const csdb::Transaction& trans) override;

        Result onTransactionList(SolverContext& context, const csdb::Pool& pool) override;

        const char * getName() const override
        {
            return "Collect";
        }

    private:

        size_t m_cntTransactions { 0 };
    };

} // slv2
