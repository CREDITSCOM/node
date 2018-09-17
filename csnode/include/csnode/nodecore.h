#ifndef NODE_CORE_H
#define NODE_CORE_H

#include <string>
#include <vector>
#include <lib/system/keys.hpp>
#include <csdb/pool.h>
#include <unordered_map>

namespace cs
{
    // solver data
    using Vector = std::string;
    using Matrix = std::string;

    // transaction packet storage
    using TransactionsPacket = csdb::Pool;
    using TransactionsPacketHash = csdb::PoolHash;

    // hash table for fast transactions storage
    using TransactionsPacketHashTable = std::unordered_map<TransactionsPacketHash, TransactionsPacket>;

    // round data
    using RoundNumber = uint32_t;
    using ConfidantsKeys = std::vector<PublicKey>;
    using Hashes = std::vector<cs::TransactionsPacketHash>;

    enum NodeConsts
    {
        PublicKeyLength = 32,
        Black2HashLength = 32
    };

    // all info about round
    struct RoundInfo
    {
        RoundNumber round;
        PublicKey general;
        ConfidantsKeys confidants;
        Hashes hashes;
    };
}

namespace std
{
    // poolHash hash specialization
    template<>
    struct hash<cs::TransactionsPacketHash>
    {
        std::size_t operator()(const cs::TransactionsPacketHash& packetHash) const noexcept;
    };
}

#endif // NODE_CORE_H

