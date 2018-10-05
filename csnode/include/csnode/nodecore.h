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
#include <csnode/transactionsPacket.h>

namespace cs
{
    // solver data
    using Vector = std::string;
    using Matrix = std::string;

    // static byte (unsigned char) array
    template<std::size_t size>
    using ByteArray = std::array<unsigned char, size>;

    using Byte = uint8_t;
    using Bytes = std::vector<Byte>;

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
        PublicKeyLength = 32,
        Black2HashLength = 32
    };

    enum SolverConsts : uint32_t
    {
        TransactionsPacketInterval = 50,    // ms
        MaxPacketTransactions = 500
    };

    // all info about round
    struct RoundInfo
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

