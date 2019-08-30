#include "gtest/gtest.h"

#include <memory>
#include <random>
#include <vector>
#include <utility>

#include <boost/filesystem.hpp>
#include <lib/system/console.hpp>

#include <lmdb.hpp>

namespace fs = boost::filesystem;
static const std::string dbPath = "./temptestdb";

struct DbDeleter {
    void operator()(cs::Lmdb* db) {
        if (db->isOpen()) {
            db->close();
        }

        fs::path path(dbPath);
        fs::remove_all(path);

        delete db;
    }
};

using LmdbPtr = std::unique_ptr<cs::Lmdb, DbDeleter>;

static auto createDb(const std::string path = dbPath) {
    return LmdbPtr(new cs::Lmdb(path));
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
    auto db = createDb();

    db->open();
    ASSERT_TRUE(db->isOpen());

    db->close();
    ASSERT_FALSE(db->isOpen());
}

TEST(Lmdbxx, InsertStringUnnamedDb) {
    auto db = createDb();
    db->open();

    std::string key = "Key1";
    std::string value = "Value1";

    db->insert(key, value);

    ASSERT_TRUE(db->size() == 1);
    ASSERT_TRUE(db->isKeyExists(key));
}

TEST(Lmdbxx, InsertTrivial) {
    auto db = createDb();
    db->open();

    int key = 100;
    long value = 50;

    db->insert(key, value);

    ASSERT_TRUE(db->size() == 1);
    ASSERT_TRUE(db->isKeyExists(key));

    auto v = db->value<long>(key);
    ASSERT_EQ(value, v);
}

TEST(Lmdbxx, InsertGetRemoveFloatingPoint) {
    auto db = createDb();
    db->open();

    float key = 100.0f;
    double value = 50.0;

    db->insert(key, value);

    ASSERT_TRUE(db->size() == 1);
    ASSERT_TRUE(db->isKeyExists(key));

    auto v = db->value<decltype(value)>(key);
    ASSERT_EQ(int(value), int(v));

    db->remove(key);

    ASSERT_TRUE(db->size() == 0);
    ASSERT_FALSE(db->isKeyExists(key));
}

TEST(Lmdbxx, InsertAndRemove) {
    auto db = createDb();
    db->open();

    std::string key = "1111";
    db->insert(key, "value");
    db->insert("2222", "value");

    ASSERT_EQ(db->size(), 2);

    db->remove(key);
    db->remove("2222");

    ASSERT_EQ(db->size(), 0);
}

TEST(Lmdbxx, LastPair) {
    auto db = createDb();
    db->open();

    std::string key1 = "Key10";
    std::string value1 = "Value10";

    db->insert(key1, value1);

    {
        auto [key, value] = db->last<std::string, std::string>();

        ASSERT_TRUE(key == key1);
        ASSERT_TRUE(value == value1);
    }

    std::string key2 = "Key2";
    std::string value2 = "Value2";

    db->insert(key2, value2);

    {
        auto [key, value] = db->last<std::string, std::string>();

        ASSERT_TRUE(key == key2);
        ASSERT_TRUE(value == value2);
    }
}

TEST(Lmdbxx, TestMappedSize) {
    auto db = createDb();
    db->setMapSize(9000);
    db->open();

    cs::Connector::connect(&db->failed, [](const auto& e) {
        cs::Console::writeLine("Error in database ", e.what());
    });

    cs::Console::writeLine("Db map size ", db->mapSize());
    ASSERT_TRUE(db->mapSize() == 9000);

    std::string key = "key";
    std::string value = "SuperAndLargeValue";
    const size_t modifier = 10;

    for (size_t i = 0; i < modifier; ++i) {
        value.append("1");
    }

    cs::Console::writeLine("Key + value size ", key.size() + value.size());

    db->setMapSize(cs::Lmdb::Default1GbMapSize);
    db->insert(key, value);

    ASSERT_TRUE(db->isKeyExists(key));
    ASSERT_TRUE(db->size() == 1);

    ASSERT_TRUE(db->value<std::string>(key) == value);
}

TEST(Lmdbxx, TestAutoMapSizeIncrease) {
    bool isIncreased = false;
    size_t reallocatesCount = 0;

    auto db = createDb();

    cs::Connector::connect(&db->failed, [](const auto& e) {
        cs::Console::writeLine("Error in database ", e.what());
    });

    cs::Connector::connect(&db->mapSizeIncreased, [&](const size_t) {
        isIncreased = true;
        ++reallocatesCount;
    });

    db->setMapSize(9000);
    db->setIncreaseSize(50000);
    db->open();

    std::string key = "Key";
    std::string value = "Value";
    constexpr size_t count = 10000;

    for (size_t i = 0; i < count; ++i) {
        auto newKey = key + std::to_string(i);
        auto newValue = value + std::to_string(i);

        db->insert(newKey, newValue);

        auto temp = db->value<std::string>(newKey);
        ASSERT_EQ(temp, newValue);
    }

    ASSERT_EQ(db->size(), count);
    ASSERT_TRUE(isIncreased);

    cs::Console::writeLine("Reallocates count ", reallocatesCount);
}

TEST(Lmdbxx, TestDefaultValueRewrite) {
    auto db = createDb();
    db->open();

    std::string key = "Key";
    std::string value = "NewValue";

    db->insert(key, "Value");
    db->insert(key, value);

    ASSERT_TRUE(db->size() == 1);
    ASSERT_EQ(db->value<std::string>(key), value);
}

TEST(Lmdbxx, TestStringView) {
    auto db = createDb();
    db->open();

    db->insert("Key", "Value");

    ASSERT_TRUE(db->value<std::string_view>("Key") == "Value");
}

TEST(Lmdbxx, TestLongValuesArrayCast) {
    auto db = createDb();
    db->open();

    unsigned short value1 = std::numeric_limits<decltype(value1)>::max();
    unsigned int value2 = std::numeric_limits<decltype(value2)>::max();
    unsigned long long value3 = std::numeric_limits<decltype(value3)>::max();

    db->insert("Key1", value1);
    db->insert("Key2", value2);
    db->insert("Key3", value3);

    auto expectedValue1 = db->value<decltype(value1)>("Key1");
    auto expectedValue2 = db->value<decltype(value2)>("Key2");
    auto expectedValue3 = db->value<decltype(value3)>("Key3");

    ASSERT_EQ(value1, expectedValue1);
    ASSERT_EQ(value2, expectedValue2);
    ASSERT_EQ(value3, expectedValue3);
}

using Elements = std::pair<std::string, std::string>;
using KeyValueStorage = std::vector<Elements>;

struct Storage {
static inline Elements getElementFromStorage(KeyValueStorage& storage) {
    [[maybe_unused]] static std::mt19937 mersenneTwister(std::random_device{}());
    std::uniform_int_distribution<int> distribution(0, static_cast<int>(storage.size() - 1));

    auto index = static_cast<size_t>(distribution(mersenneTwister));
    auto iter = storage.begin();

    std::advance(iter, index);

    auto elements = (*iter);
    storage.erase(iter);

    return elements;
}
};

TEST(Lmdbxx, HighLoadTest) {
    auto db = createDb();
    db->open();

    cs::Connector::connect(&db->failed, [](const auto& e) {
        cs::Console::writeLine("Error in database ", e.what());
    });

#ifdef NDEBUG
    constexpr size_t elementsCount = 50000;
#else
    constexpr size_t elementsCount = 1000;
#endif

    std::string key = "Key";
    std::string value = "Value";

    KeyValueStorage storage;
    storage.reserve(elementsCount);

    for (size_t i = 0; i < elementsCount; ++i) {
        auto str = std::to_string(i);

        auto k = key + str;
        auto v = value + str;

        db->insert(k, v);
        storage.push_back(std::make_pair(std::move(k), std::move(v)));
    }

    ASSERT_EQ(db->size(), storage.size());

    // try to load
    while (!storage.empty()) {
        auto [k, v] = Storage::getElementFromStorage(storage);

        {
            auto expectedValue = db->value<std::string_view>(k);
            ASSERT_EQ(expectedValue, v);
        }

        db->remove(k);
    }

    ASSERT_TRUE(db->isEmpty());
}

TEST(Lmdbxx, DISABLED_InsertInDifferentTables) {
    auto db = createDb();

    cs::Connector::connect(&db->failed, [](const auto& e) {
        cs::Console::writeLine("Error in database ", e.what());
    });

    db->setMaxDbs(2);
    db->setMaxReaders(2);
    db->open();

    const char* db1 = "Table1";
    const char* db2 = "Table2";

    std::string key1 = "Key1";
    std::string key2 = "Key2";

    std::string value1 = "Value1";
    std::string value2 = "Value2";

    db->insert(key1, value1, db1, MDB_CREATE);
    db->insert(key2, value2, db2, MDB_CREATE);

    cs::Console::writeLine("Size of ", db1, " ", db->size(db1));
    cs::Console::writeLine("Size of ", db2, " ", db->size(db2));

    ASSERT_TRUE(db->size(db1) == 1);
    ASSERT_TRUE(db->size(db2) == 1);

    ASSERT_TRUE(db->isKeyExists(key1, db1));
    ASSERT_TRUE(db->isKeyExists(key2, db2));
}
