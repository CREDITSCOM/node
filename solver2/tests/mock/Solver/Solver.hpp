#pragma once
#include <gmock/gmock.h>
#include <cstdint>

constexpr const size_t MAX_CONF_NUMBER = 5;

class Node;
namespace csdb
{
    class Pool;
}

namespace Credits
{
    struct Hash_
    {
        uint8_t val[32];
    };
    
    struct Signature
    {};

    struct StageOne
    {
        uint8_t sender { 0 };
        Hash_ hash;
        uint8_t candidatesAmount { 0 };
        PublicKey candiates[MAX_CONF_NUMBER];
        Signature sig;
    };

    struct StageTwo
    {
        uint8_t sender { 0 };
        uint8_t trustedAmount { 0 };
        Signature signatures[MAX_CONF_NUMBER];
        Signature sig;
    };

    struct StageThree
    {
        uint8_t sender { 0 };
        uint8_t writer { 0 };
        Hash_ hashBlock;
        Hash_ hashCandidatesList;
        Signature sig;
    };

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