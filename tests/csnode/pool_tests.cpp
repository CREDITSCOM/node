#define TESTING

#include "gtest/gtest.h"

#include <cstdint>
#include <cstring>
#include <limits>

#include <csdb/pool.hpp>

// serialized pool and its hashing length (written as a raw size_t right after the hashed prefix)
static cs::Bytes poolBinary(size_t& hashingLength) {
    csdb::Pool pool;
    pool.set_sequence(1);

    cs::Bytes bytes = pool.to_binary_updated();
    hashingLength = pool.hashingLength();

    return bytes;
}

TEST(Pool, AcceptsSerializedPool) {
    size_t hashingLength = 0;
    cs::Bytes bytes = poolBinary(hashingLength);

    ASSERT_TRUE(csdb::Pool::from_binary(std::move(bytes)).is_valid());
}

// pool bytes come from the network (blocks requested while syncing)
TEST(Pool, RejectsHashingLengthPastData) {
    size_t hashingLength = 0;
    cs::Bytes bytes = poolBinary(hashingLength);

    ASSERT_GT(hashingLength, 0u);
    ASSERT_LE(hashingLength + sizeof(size_t), bytes.size());

    const size_t forged = bytes.size() + 1024 * 1024;
    std::memcpy(bytes.data() + hashingLength, &forged, sizeof(forged));

    ASSERT_FALSE(csdb::Pool::from_binary(cs::Bytes(bytes)).is_valid());
    ASSERT_TRUE(csdb::Pool::hash_from_binary(cs::Bytes(bytes)).is_empty());
}

// offset of the transactions count: the only uint32 that makes meta_from_binary() report it
static size_t transactionsCountOffset(const cs::Bytes& bytes) {
    const uint32_t marker = std::numeric_limits<uint32_t>::max();

    for (size_t offset = 0; offset + sizeof(marker) <= bytes.size(); ++offset) {
        cs::Bytes copy = bytes;
        std::memcpy(copy.data() + offset, &marker, sizeof(marker));

        size_t count = 0;
        if (csdb::Pool::meta_from_binary(std::move(copy), count).is_valid() && count == marker) {
            return offset;
        }
    }

    return bytes.size();
}

// counts come from the data: a forged one must not turn into a huge allocation
TEST(Pool, HugeTransactionsCountDoesNotAllocate) {
    size_t hashingLength = 0;
    cs::Bytes bytes = poolBinary(hashingLength);

    const size_t offset = transactionsCountOffset(bytes);
    ASSERT_LT(offset, bytes.size());

    const uint32_t forged = std::numeric_limits<uint32_t>::max();
    std::memcpy(bytes.data() + offset, &forged, sizeof(forged));

    csdb::Pool pool;
    ASSERT_NO_THROW(pool = csdb::Pool::from_binary(std::move(bytes)));
    ASSERT_FALSE(pool.is_valid());
}

// with no transactions, the new wallets count follows the transactions count
TEST(Pool, HugeNewWalletsCountDoesNotAllocate) {
    size_t hashingLength = 0;
    cs::Bytes bytes = poolBinary(hashingLength);

    const size_t offset = transactionsCountOffset(bytes) + sizeof(uint32_t);
    ASSERT_LE(offset + sizeof(uint32_t), bytes.size());

    const uint32_t forged = std::numeric_limits<uint32_t>::max();
    std::memcpy(bytes.data() + offset, &forged, sizeof(forged));

    csdb::Pool pool;
    ASSERT_NO_THROW(pool = csdb::Pool::from_binary(std::move(bytes)));
    ASSERT_FALSE(pool.is_valid());
}
