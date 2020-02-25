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
using TransactionsPacketTable = std::map<TransactionsPacketHash, TransactionsPacket>;   // to be sorted by default

// array of notifications
using Notifications = std::vector<cs::Bytes>;
using Signatures = std::vector<cscrypto::Signature>;
// round data
using PublicKeys = std::vector<PublicKey>;
using PrivateKeys = std::vector<PrivateKey>;

using ConfidantsKeys = PublicKeys;
using PacketsHashes = std::vector<cs::TransactionsPacketHash>;
using PacketsVector = std::vector<cs::TransactionsPacket>;
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
    TransactionsPacketInterval = 250
};

enum ConfidantConsts : Byte {
    FirstWriterIndex = std::numeric_limits<uint8_t>::min(),
    LiarIndex = 254,
    InvalidConfidantIndex = std::numeric_limits<uint8_t>::max(),
};

enum SpoilingConsts : Byte {
    SpoilByRoundNumber,
    SpoilByPublicKey,
};

enum class PoolStoreType : cs::Byte {
    Created,
    Synced
};

// all info about round
struct Characteristic {
    cs::Bytes mask;
};

struct RoundTable {
    cs::Bytes toBinary() const;
    RoundNumber round = 0;
    ConfidantsKeys confidants;
    PacketsHashes hashes;
};

struct PoolMetaInfo {
    Characteristic characteristic;
    std::string timestamp;
    csdb::PoolHash previousHash;
    cs::Sequence sequenceNumber;
    cs::Bytes realTrustedMask;
    std::vector <csdb::Pool::SmartSignature> smartSignatures;
};

// metas
struct PoolSyncMeta {
    csdb::Pool pool;
    cs::Signature signature;
    cs::PublicKey sender;
};

struct Command{
    cs::RoundNumber rNum;
    size_t intParam;
    cs::Bytes data;
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

struct TimeMoney {
    TimeMoney(uint64_t t, csdb::Amount am);
    uint64_t time;
    csdb::Amount amount;
};
// transactions user fields
namespace trx_uf {
    // delegation transaction fields
    namespace sp { //specific
        // delegation
        constexpr csdb::user_field_id_t delegated = 5; // value: 1 - delegation, 2 - withdraw delegation
        constexpr csdb::user_field_id_t managing = 7;
        namespace de {
            constexpr uint64_t legate = 1;
            constexpr uint64_t legated_withdraw = 2;
            constexpr uint64_t legated_release = 3;
            constexpr uint64_t legate_min_utc = 4;
        }

        // count of user fields
        constexpr size_t Count = 1;
    }
}

// meta storages
using ConveyerMetaStorage = cs::MetaStorage<cs::ConveyerMeta>;
using CharacteristicMetaStorage = cs::MetaStorage<cs::CharacteristicMeta>;

// zero constants, used as "empty"
struct Zero {
    inline static cs::Hash hash;
    inline static cs::Signature signature;
    inline static cs::PublicKey key;
	inline static uint64_t timeStamp;
    Zero();
};
}  // namespace cs

#endif  // NODE_CORE_HPP
