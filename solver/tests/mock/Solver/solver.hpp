#pragma once
#include <gmock/gmock.h>
#include <cstdint>

constexpr const size_t MAX_CONF_NUMBER = 5;

class Node;
namespace csdb
{
    class Pool;
}

namespace cs
{
    class Fee
    {
    public:

        MOCK_METHOD2(CountFeesInPool, void(const Node*, const csdb::Pool*));
    };

    class Solver
	{
	public:
	};
}