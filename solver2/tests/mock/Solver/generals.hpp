#pragma once

#include <csdb/pool.h>
#include <Solver/Solver.hpp>

namespace cs
{
    class Generals
    {
    public:
        Generals()
        {}

        Generals(const Generals &)
        {}

        MOCK_METHOD2(takeUrgentDecision, uint8_t(size_t, const csdb::PoolHash&));
        MOCK_METHOD3(buildvector, Hash(csdb::Pool&, csdb::Pool&, csdb::Pool&));
    };
}
