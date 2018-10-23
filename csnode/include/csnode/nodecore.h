#ifndef NODE_CORE_H
#define NODE_CORE_H

#include <string>
#include <vector>
#include <array>
#include <lib/system/keys.hpp>
#include <csdb/pool.h>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <shared_mutex>
#include <boost/smart_ptr/detail/spinlock.hpp>
#include <libcuckoo/cuckoohash_map.hh>
#include <csnode/transactionspacket.h>
#include <lib/system/common.hpp>

namespace cs
{
    // hash table for fast transactions storage
    using TransactionsPacketHashTable = std::unordered_map<TransactionsPacketHash, TransactionsPacket>;

    // array of packets
    using TransactionsBlock = std::vector<cs::TransactionsPacket>;

    // array of notifications
    using Notifications = std::vector<cs::Bytes>;

    // round data
    using RoundNumber = uint32_t;
    using ConfidantsKeys = std::vector<PublicKey>;
    using Hashes = std::vector<cs::TransactionsPacketHash>;
    using HashesSet = std::unordered_set<cs::TransactionsPacketHash>;

    // sync types
    using SharedMutex = std::shared_mutex;
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
        TransactionsFlushRound = 2,
        TransactionsPacketInterval = 50,    // ms
        MaxPacketTransactions = 500,
        RoundDelay = 1000                   // delay
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
        cs::Bytes mask;
    };

    struct PoolMetaInfo
    {
        std::string timestamp;
        csdb::Pool::sequence_t sequenceNumber;
    };

    struct HashVector
    {
        uint8_t sender;
        cs::Hash hash;
        cs::Signature signature;
    };

    const std::size_t hashVectorCount = 5;

    struct HashMatrix
    {
        uint8_t sender;
        HashVector hashVector[hashVectorCount];
        cs::Signature signature;
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
