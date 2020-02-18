#define TESTING

#include <memory>

#include "gtest/gtest.h"

#include <lib/system/fileutils.hpp>

#include <csnode/poolcache.hpp>

#include <csdb/currency.hpp>
#include <csdb/amount.hpp>
#include <csdb/amount_commission.hpp>

static const std::string dbPath = "./temppoolcaches";

struct CacheDeleter {
    void operator()(cs::PoolCache* db) {
        delete db;
        cs::FileUtils::removePath(dbPath);
    }
};

using PoolCachePtr = std::unique_ptr<cs::PoolCache, CacheDeleter>;

static PoolCachePtr createPoolCache() {
    cs::FileUtils::createPathIfNoExist(dbPath);
    return std::unique_ptr<cs::PoolCache, CacheDeleter>(new cs::PoolCache(dbPath));
}

static csdb::Transaction createTestTransaction(const int64_t id, const uint8_t amount) {
    cs::Signature sign{};
    csdb::Transaction transaction{id,
                                  csdb::Address::from_public_key(cs::PublicKey{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                                                               0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01}),
                                  csdb::Address::from_public_key(cs::PublicKey{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                                                               0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02}),
                                  csdb::Currency{amount},
                                  csdb::Amount{0, 0},
                                  csdb::AmountCommission{0.},
                                  csdb::AmountCommission{0.},
                                  sign};
    return transaction;
}

TEST(PoolCache, Insert) {
    const size_t value = 10;
    auto cache = createPoolCache();
    cache->insert(value, {0}, cs::PoolStoreType::Created);

    ASSERT_TRUE(cache->contains(value));
}

TEST(PoolCache, Remove) {
    const size_t value = 10;
    auto cache = createPoolCache();
    cache->insert(value, {0}, cs::PoolStoreType::Created);

    ASSERT_TRUE(cache->contains(value));
    ASSERT_TRUE(cache->remove(value));
    ASSERT_FALSE(cache->contains(value));
}

TEST(PoolCache, TestCount) {
    auto cache = createPoolCache();
    cache->insert(1, {0}, cs::PoolStoreType::Created);
    cache->insert(2, {0}, cs::PoolStoreType::Created);
    cache->insert(3, {0}, cs::PoolStoreType::Synced);
    cache->insert(4, {0}, cs::PoolStoreType::Synced);
    cache->insert(5, {0}, cs::PoolStoreType::Synced);

    ASSERT_EQ(cache->size(), 5);
    ASSERT_EQ(cache->sizeSynced(), 3);
    ASSERT_EQ(cache->sizeCreated(), 2);
}

TEST(PoolCache, TestPoolCorrectSerialization) {
    auto cache = createPoolCache();
    csdb::Pool pool;

    pool.set_sequence(10);
    pool.add_transaction(createTestTransaction(1, 100));
    pool.compose();

    cache->insert(pool, cs::PoolStoreType::Created);

    auto data = cache->pop(10);
    ASSERT_TRUE(data.has_value());
    ASSERT_EQ(cs::PoolStoreType::Created, data.value().type);

    ASSERT_EQ(pool.sequence(), data.value().pool.sequence());
    ASSERT_EQ(pool.transactions().front().to_binary(), data.value().pool.transactions().front().to_binary());

    ASSERT_TRUE(cache->isEmpty());
}

static csdb::Pool createPool(cs::Sequence sequence, uint8_t amount = 100) {
    csdb::Pool pool;

    pool.set_sequence(sequence);
    pool.add_transaction(createTestTransaction(static_cast<int64_t>(sequence), amount));
    pool.compose();

    return pool;
}

TEST(PoolCache, TestHighLoadPoolSerialization) {
    const static size_t poolsCount = 10000;
    auto cache = createPoolCache();

    std::vector<csdb::Pool> expectedPools;

    for (size_t i = 0; i < poolsCount; ++i) {
        expectedPools.push_back(createPool(i));
    }

    for (const auto& pool : expectedPools) {
        cache->insert(pool, cs::PoolStoreType::Created);
    }

    std::vector<csdb::Pool> currentPools;

    for (size_t i = 0; i < poolsCount; ++i) {
        ASSERT_TRUE(cache->contains(i));

        auto data = cache->value(i);

        ASSERT_TRUE(data.has_value());
        ASSERT_EQ(data.value().pool.sequence(), i);
        currentPools.push_back(data.value().pool);
    }

    for (size_t i = 0; i < poolsCount; ++i) {
        auto& lhs = expectedPools[i];
        auto& rhs = currentPools[i];

        ASSERT_EQ(lhs.sequence(), rhs.sequence());
        ASSERT_EQ(lhs.to_binary(), rhs.to_binary());
    }
}
