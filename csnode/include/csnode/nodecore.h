#ifndef NODE_CORE_H
#define NODE_CORE_H

#include <string>
#include <vector>
#include <array>
#include <lib/system/keys.hpp>
#include <csdb/pool.h>
#include <unordered_map>
#include <mutex>
#include <shared_mutex>
#include <boost/thread/shared_mutex.hpp>
#include <boost/smart_ptr/detail/spinlock.hpp>
#include <libcuckoo/cuckoohash_map.hh>
#include <csnode/transactionspacket.h>
#include <lib/system/common.hpp>

namespace cs
{
    // hash table for fast transactions storage
    using TransactionsPacketHashTable = cuckoohash_map<TransactionsPacketHash, TransactionsPacket>;

    // array of packets
    using TransactionsBlock = std::vector<cs::TransactionsPacket>;

    // round data
    using RoundNumber = uint32_t;
    using ConfidantsKeys = std::vector<PublicKey>;
    using Hashes = std::vector<cs::TransactionsPacketHash>;

    // sync types
    using SharedMutex = boost::shared_mutex;    // C++17 compliler std::shared_mutex
    using SpinLock = boost::detail::spinlock;

    // RAII locks
    using Lock = std::lock_guard<cs::SharedMutex>;
    using SharedLock = std::shared_lock<cs::SharedMutex>;
    using SpinGuard = std::lock_guard<SpinLock>;

    enum NodeConsts : uint32_t
    {
        PublicKeyLength = PUBLIC_KEY_LENGTH,
        Black2HashLength = BLAKE2_HASH_LENGTH,
        HashLength = HASH_LENGTH,
        SignatureLength = SIGNATURE_LENGTH,
        PrivateKeyLength = PRIVATE_KEY_LENGTH
    };

    enum SolverConsts : uint32_t
    {
        TransactionsPacketInterval = 50,    // ms
        MaxPacketTransactions = 500
    };

    // all info about round
    struct RoundTable
    {
        RoundNumber round;
        PublicKey general;
        ConfidantsKeys confidants;
        Hashes hashes;
    };

    struct Characteristic
    {
        uint32_t size = 0;
        std::vector<uint8_t> mask;
    };

    struct PoolMetaInfo
    {
        std::string timestamp;
        csdb::Pool::sequence_t sequenceNumber;
    };
}

namespace std
{
    // transactions packet hash specialization
    template<>
    struct hash<cs::TransactionsPacketHash>
    {
        std::size_t operator()(const cs::TransactionsPacketHash& packetHash) const noexcept;
    };
}

#endif // NODE_CORE_H

