#ifndef NODE_CORE_HPP
#define NODE_CORE_HPP

#include <csdb/pool.hpp>
#include <map>
#include <unordered_map>
#include <unordered_set>

#include <boost/circular_buffer.hpp>

#include <client/params.hpp>
#include <csnode/transactionspacket.hpp>

#include <lib/system/common.hpp>
#include <lib/system/metastorage.hpp>

namespace std {
// transactions packet hash specialization
template <>
struct hash<cs::TransactionsPacketHash> {
    std::size_t operator()(const cs::TransactionsPacketHash& packetHash) const noexcept;
};
}  // namespace std

namespace cs {
// table for fast transactions storage
using TransactionsPacketTable = std::map<TransactionsPacketHash, TransactionsPacket>;  // TODO: check performance of map/unordered_map

// array of packets
using TransactionsBlock = std::vector<cs::TransactionsPacket>;

// array of notifications
using Notifications = std::vector<cs::Bytes>;
using Signatures = std::vector<cscrypto::Signature>;
// round data
using PublicKeys = std::vector<PublicKey>;
using PrivateKeys = std::vector<PrivateKey>;

using ConfidantsKeys = PublicKeys;
using PacketsHashes = std::vector<cs::TransactionsPacketHash>;
using Packets = std::vector<cs::TransactionsPacket>;
using Signatures = std::vector<cs::Signature>;
using Hashes = std::vector<cs::Hash>;

using PoolsRequestedSequences = std::vector<cs::Sequence>;
using PoolsBlock = std::vector<csdb::Pool>;

enum NodeConsts : uint32_t {
    NeighboursRequestDelay = 350,
    MaxRoundDeltaInStopRequest = 100 ///< Max allowed round difference in NodeStopRequest, otherwise ignore the command
};

enum ConveyerConsts : uint32_t {
    TransactionsFlushRound = 2,
    TransactionsPacketInterval = 1000
};

enum ConfidantConsts : Byte {
    FirstWriterIndex = std::numeric_limits<uint8_t>::min(),
    InvalidConfidantIndex = std::numeric_limits<uint8_t>::max(),
};

enum SpoilingConsts : Byte {
    SpoilByRoundNumber,
    SpoilByPublicKey,
};

// all info about round
struct Characteristic {
    cs::Bytes mask;
};

struct RoundTable {
    RoundNumber round = 0;
    PublicKey general;
    ConfidantsKeys confidants;
    PacketsHashes hashes;
    Characteristic characteristic;
};

struct PoolMetaInfo {
    std::string timestamp;
    cs::PublicKey writerKey;
    csdb::PoolHash previousHash;
    cs::Sequence sequenceNumber;
    cs::Bytes realTrustedMask;
    std::vector<csdb::Pool::SmartSignature> smartSignatures;
    cs::Bytes confirmationMask;
    cs::Signatures confirmations;
};

struct HashVector {
    cs::Byte sender;
    cs::Hash hash;
    cs::Signature signature;
};

constexpr std::size_t kHashVectorCount = 5;

struct HashMatrix {
    cs::Byte sender;
    cs::HashVector hashVector[kHashVectorCount];
    cs::Signature signature;
};

// metas
struct PoolSyncMeta {
    csdb::Pool pool;
    cs::Signature signature;
    cs::PublicKey sender;
};

using PoolMetaMap = std::map<cs::Sequence, cs::PoolSyncMeta>;

struct ConveyerMeta {
    cs::Characteristic characteristic;
    cs::TransactionsPacketTable hashTable;
    cs::PacketsHashes neededHashes;
    cs::RoundTable roundTable;
    cs::Notifications notifications;
    cs::TransactionsPacket invalidTransactions;
};

struct CharacteristicMeta {
    cs::Bytes bytes;
    cs::PublicKey sender;
    cs::Signatures signatures;
    cs::Bytes realTrusted;
};

struct RoundTableMessage {
    cs::Bytes message;
    cs::RoundNumber round = 0;
    cs::PublicKey sender;
};

// meta storages
using ConveyerMetaStorage = cs::MetaStorage<cs::ConveyerMeta>;
using CharacteristicMetaStorage = cs::MetaStorage<cs::CharacteristicMeta>;

// zero constants, used as "empty"
struct Zero {
    inline static cs::Hash hash;
    inline static cs::Signature signature;
    inline static cs::PublicKey key;

    Zero();
};
}  // namespace cs

#endif  // NODE_CORE_HPP
