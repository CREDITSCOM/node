#include "gtest/gtest.h"

#include <boost/filesystem.hpp>
#include <lib/system/console.hpp>

#include <lmdb.hpp>

namespace fs = boost::filesystem;
static const std::string dbPath = "./temptestdb";

static void removePath(cs::Lmdb& db) {
    if (db.isOpen()) {
        db.close();
    }

    fs::path path(dbPath);
    fs::remove_all(path);
}

TEST(Lmdbxx, PathCreation) {
    {
        cs::Lmdb db(dbPath);
    }

    fs::path path(dbPath);
    ASSERT_TRUE(fs::is_directory(path));

    fs::remove_all(path);
    ASSERT_FALSE(fs::is_directory(path));
}

TEST(Lmdbxx, DatabaseOpenClose) {
    cs::Lmdb db(dbPath);

    db.open();
    ASSERT_TRUE(db.isOpen());

    db.close();
    ASSERT_FALSE(db.isOpen());

    removePath(db);
}

TEST(Lmdbxx, InsertStringUnnamedDb) {
    cs::Lmdb db(dbPath);
    db.open();

    std::string key = "Key1";
    std::string value = "Value1";

    db.insert(key, value);

    ASSERT_TRUE(db.size() == 1);
    ASSERT_TRUE(db.isKeyExists(key));

    removePath(db);
}

TEST(Lmdbxx, InsertTrivial) {
    cs::Lmdb db(dbPath);
    db.open();

    int key = 100;
    long value = 50;

    db.insert(key, value);

    ASSERT_TRUE(db.size() == 1);
    ASSERT_TRUE(db.isKeyExists(key));

    auto v = db.value<long>(key);
    ASSERT_EQ(value, v);

    removePath(db);
}

TEST(Lmdbxx, LastPair) {
    cs::Lmdb db(dbPath);
    db.open();

    std::string key1 = "Key10";
    std::string value1 = "Value10";

    db.insert(key1, value1);

    {
        auto [key, value] = db.last<std::string, std::string>();

        ASSERT_TRUE(key == key1);
        ASSERT_TRUE(value == value1);
    }

    std::string key2 = "Key2";
    std::string value2 = "Value2";

    db.insert(key2, value2);

    {
        auto [key, value] = db.last<std::string, std::string>();

        ASSERT_TRUE(key == key2);
        ASSERT_TRUE(value == value2);
    }

    removePath(db);
}

TEST(Lmdbxx, TestMappedSize) {
    cs::Lmdb db(dbPath);
    db.setMapSize(9000);
    db.open();

    cs::Connector::connect(&db.failed, [](const auto& e) {
        cs::Console::writeLine("Error in database ", e.what());
    });

    cs::Console::writeLine("Db map size ", db.mapSize());
    ASSERT_TRUE(db.mapSize() == 9000);

    std::string key = "key";
    std::string value = "SuperAndLargeValue";
    const size_t modifier = 10;

    for (size_t i = 0; i < modifier; ++i) {
        value.append("1");
    }

    cs::Console::writeLine("Key + value size ", key.size() + value.size());

    db.setMapSize(cs::Lmdb::Default1GbMapSize);
    db.insert(key, value);

    ASSERT_TRUE(db.isKeyExists(key));
    ASSERT_TRUE(db.size() == 1);

    ASSERT_TRUE(db.value<std::string>(key) == value);

    removePath(db);
}

TEST(Lmdbxx, TestAutoMapSizeIncrease) {
    bool isIncreased = false;
    size_t reallocatesCount = 0;

    cs::Lmdb db(dbPath);

    cs::Connector::connect(&db.failed, [](const auto& e) {
        cs::Console::writeLine("Error in database ", e.what());
    });

    cs::Connector::connect(&db.mapSizeIncreased, [&](const size_t) {
        isIncreased = true;
        ++reallocatesCount;
    });

    db.setMapSize(9000);
    db.setIncreaseSize(50000);
    db.open();

    std::string key = "Key";
    std::string value = "Value";
    constexpr size_t count = 10000;

    for (size_t i = 0; i < count; ++i) {
        auto newKey = key + std::to_string(i);
        auto newValue = value + std::to_string(i);

        db.insert(newKey, newValue);

        auto temp = db.value<std::string>(newKey);
        ASSERT_EQ(temp, newValue);
    }

    ASSERT_EQ(db.size(), count);
    ASSERT_TRUE(isIncreased);

    cs::Console::writeLine("Reallocates count ", reallocatesCount);

    removePath(db);
}

TEST(Lmdbxx, DISABLED_InsertInDifferentTables) {
    cs::Lmdb db(dbPath);

    cs::Connector::connect(&db.failed, [](const auto& e) {
        cs::Console::writeLine("Error in database ", e.what());
    });

    db.setMaxDbs(2);
    db.setMaxReaders(2);
    db.open();

    const char* db1 = "Table1";
    const char* db2 = "Table2";

    std::string key1 = "Key1";
    std::string key2 = "Key2";

    std::string value1 = "Value1";
    std::string value2 = "Value2";

    db.insert(key1, value1, db1, MDB_CREATE);
    db.insert(key2, value2, db2, MDB_CREATE);

    cs::Console::writeLine("Size of ", db1, " ", db.size(db1));
    cs::Console::writeLine("Size of ", db2, " ", db.size(db2));

    ASSERT_TRUE(db.size(db1) == 1);
    ASSERT_TRUE(db.size(db2) == 1);

    ASSERT_TRUE(db.isKeyExists(key1, db1));
    ASSERT_TRUE(db.isKeyExists(key2, db2));

    removePath(db);
}
