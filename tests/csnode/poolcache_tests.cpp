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
    return std::unique_ptr<cs::PoolCache, CacheDeleter>(new cs::PoolCache(dbPath));
}

TEST(PoolCache, Insert) {
    auto cache = createPoolCache();
    cache->insert(10, {0});

    ASSERT_TRUE(cache->contains(10));
}
