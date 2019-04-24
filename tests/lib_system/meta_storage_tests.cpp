#include "gtest/gtest.h"

#include <iostream>
#include <lib/system/metastorage.hpp>
#include <string>

using TestIntMetaSatorage = cs::MetaStorage<int>;
using TestStringMetaStorage = cs::MetaStorage<std::string>;

template <typename T>
void printStorageSize(const T& storage) {
    std::cout << "Storage size " << storage.size() << std::endl;
}

template <typename T>
void printValue(const std::string& message, const T& value) {
    std::cout << message << " " << value << std::endl;
}

TEST(MetaStorage, DefaultCreation) {
    constexpr auto defaultSize = 0;
    TestIntMetaSatorage storage;

    printStorageSize(storage);
    std::cout << "Default size " << defaultSize << std::endl;

    ASSERT_EQ(storage.size(), defaultSize);
}

TEST(MetaStorage, Contains) {
    cs::RoundNumber defaultRoundNumber = 10;
    int value = 100;
    TestIntMetaSatorage storage;

    ASSERT_EQ(storage.append(defaultRoundNumber, std::move(value)), true);
    ASSERT_EQ(storage.contains(defaultRoundNumber), true);

    printStorageSize(storage);

    ASSERT_EQ(storage.size(), 1);
}

TEST(MetaStorage, AppendAndExtract) {
    TestIntMetaSatorage storage;
    cs::RoundNumber defaultRoundNumber = 20;
    int value = 300;

    printStorageSize(storage);

    ASSERT_EQ(storage.append(defaultRoundNumber, std::move(value)), true);
    ASSERT_EQ(storage.contains(defaultRoundNumber), true);
    ASSERT_EQ(storage.size(), 1);

    std::optional<int> extractedValue = storage.extract(defaultRoundNumber);

    ASSERT_EQ(extractedValue.has_value(), true);

    printStorageSize(storage);
    printValue("Extracted value", extractedValue.value());

    ASSERT_EQ(value, extractedValue.value());
    ASSERT_EQ(storage.size(), 0);

    std::cout << "Try to add next element" << std::endl;

    TestIntMetaSatorage::MetaElement element = {defaultRoundNumber, value};

    ASSERT_EQ(storage.append(element), true);
    printStorageSize(storage);

    ASSERT_EQ(storage.size(), 1);

    element.round = defaultRoundNumber + 1;
    ASSERT_EQ(storage.append(element), true);

    printStorageSize(storage);
    ASSERT_EQ(storage.size(), 2);
}

TEST(MetaStorage, GetMethod) {
    cs::RoundNumber defaultRoundNumber = 100;
    std::string defaultString = "Hello, world!";

    TestStringMetaStorage::Element element;
    element.round = defaultRoundNumber;
    element.meta = defaultString;

    TestStringMetaStorage storage;

    ASSERT_EQ(storage.append(element), true);

    std::string* stringPointer = storage.get(defaultRoundNumber);
    ASSERT_NE(stringPointer, nullptr);

    printValue("String pointer value", *stringPointer);
    ASSERT_EQ(*stringPointer, defaultString);

    printStorageSize(storage);
    ASSERT_EQ(storage.size(), 1);
}

TEST(MetaStorage, BeginEnd) {
    TestStringMetaStorage storage;
    printStorageSize(storage);

    for (const TestStringMetaStorage::Element& element : storage) {
        printValue("Storage element meta", element.meta);
        printValue("Storage element round", element.round);

        ASSERT_EQ(element.meta, std::string());
        ASSERT_EQ(element.round, cs::RoundNumber());
    }
}

TEST(MetaStorage, MaxAndLast) {
    cs::RoundNumber defaultRound = 1;
    const std::string defaultString = "qwerty";
    std::string value = defaultString;
    const std::string expectedString = "111";

    TestStringMetaStorage storage;
    printStorageSize(storage);

    // 1
    storage.append(defaultRound, std::move(value));
    value = defaultString;

    // 2
    storage.append(++defaultRound, std::move(value));
    value = defaultString;

    // 3
    storage.append(++defaultRound, std::move(value));
    value = expectedString;
    defaultRound += 7;

    // 10
    storage.append(defaultRound, std::move(value));
    printStorageSize(storage);

    ASSERT_EQ(storage.size(), 4);

    printValue("max() string ", storage.max());
    printValue("Expected string ", expectedString);

    ASSERT_EQ(storage.max(), expectedString);

    printValue("last() value ", storage.last());
    printValue("Expected last value ", expectedString);

    ASSERT_EQ(storage.last(), expectedString);
}
