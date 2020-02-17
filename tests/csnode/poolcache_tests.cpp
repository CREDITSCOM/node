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

    cache->insert(1, pool.to_binary(), cs::PoolStoreType::Created);

    auto [p, type] = cache->pop(1);
    ASSERT_EQ(cs::PoolStoreType::Created, type);

    ASSERT_EQ(pool.sequence(), p.sequence());
    ASSERT_EQ(pool.transactions().front().to_binary(), p.transactions().front().to_binary());

    ASSERT_TRUE(cache->isEmpty());
}
