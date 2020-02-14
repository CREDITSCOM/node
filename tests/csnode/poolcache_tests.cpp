#define TESTING

#include <memory>

#include "gtest/gtest.h"

#include <lib/system/fileutils.hpp>

#include <csnode/poolcache.hpp>

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

TEST(PoolCache, Insert) {
    const size_t value = 10;
    auto cache = createPoolCache();
    cache->insert(value, {0});

    ASSERT_TRUE(cache->contains(value));
}

TEST(PoolCache, Remove) {
    const size_t value = 10;
    auto cache = createPoolCache();
    cache->insert(value, {0});

    ASSERT_TRUE(cache->contains(value));
    ASSERT_TRUE(cache->remove(value));
    ASSERT_FALSE(cache->contains(value));
}
