#include "gtest/gtest.h"

#include <boost/filesystem.hpp>
#include <lib/system/console.hpp>

#include <lmdb.hpp>

namespace fs = boost::filesystem;
static const std::string dbPath = "./testdb";

static void removePath(cs::Lmdb& db) {
    if (db.isOpened()) {
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
    ASSERT_TRUE(db.isOpened());

    db.close();
    ASSERT_FALSE(db.isOpened());

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

TEST(Lmdbxx, TestMappedSize) {
    cs::Lmdb db(dbPath);
    db.setMapSize(10);

    cs::Connector::connect(&db.failed, [](const auto& e) {
        cs::Console::writeLine("Error in database ", e.what());
    });

    std::string key = "key";
    std::string value = "SuperAndLargeValue";
    const size_t modifier = 10;

    for (size_t i = 0; i < modifier; ++i) {
        value.append("1");
    }

    cs::Console::writeLine("Key + value size ", key.size() + value.size());

    db.open();
    db.setMapSize(cs::Lmdb::DefaultMapSize);

    db.insert(key, value);

    ASSERT_TRUE(db.isKeyExists(key));
    ASSERT_TRUE(db.size() == 1);

    ASSERT_TRUE(db.value<std::string>(key) == value);

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
