#include <gtest/gtest.h>
#include <cstring>
#include <limits>
#include <string>
#include <vector>
#include "datastream.hpp"
#include <lib/system/console.hpp>

TEST(DataStream, DataPointerIsCorrectAfterCreation) {
    char data[8] = "1234567";
    cs::IDataStream stream(data, sizeof data);
    ASSERT_EQ(stream.data(), data);
}

TEST(DataStream, SizeIsCorrect) {
    char data[8] = "1234567";
    cs::IDataStream stream(data, sizeof data);
    ASSERT_EQ(stream.size(), sizeof data);
}

TEST(DataStream, IsValidAfterCreation) {
    char data[8] = "1234567";
    cs::IDataStream stream(data, sizeof data);
    ASSERT_TRUE(stream.isValid());
}

TEST(DataStream, IsAvailableReturnsTrueIfRequestedEnough) {
    char data[8] = "1234567";
    cs::IDataStream stream(data, sizeof data);
    ASSERT_TRUE(stream.isAvailable(8));
}

TEST(DataStream, IsAvailableReturnsTrueIfRequestedZero) {
    char data[8] = "1234567";
    cs::IDataStream stream(data, sizeof data);
    ASSERT_TRUE(stream.isAvailable(0));
}

TEST(DataStream, IsAvailableReturnsFalseIfRequestedTooMany) {
    char data[8] = "1234567";
    cs::IDataStream stream(data, sizeof data);
    ASSERT_FALSE(stream.isAvailable(9));
}

TEST(DataStream, MustGetCorrectStdArrayFromStream) {
    char data[8] = "1234567";
    cs::IDataStream stream(data, sizeof data);
    auto result_array = stream.parseArray<char, sizeof data>();
    ASSERT_EQ(result_array[7], '\0');
    ASSERT_EQ(8, result_array.size());
    std::array<char, 8> test_array = {'1', '2', '3', '4', '5', '6', '7', '\0'};
    ASSERT_EQ(test_array, result_array);
}

TEST(DataStream, MustGetZeroFilledStdArrayIfRequestedMoreThanAvailable) {
    char data[8] = "1234567";
    cs::IDataStream stream(data, sizeof data);
    auto result_array = stream.parseArray<char, 10>();
    ASSERT_EQ(result_array.size(), 10);
    for (auto i = 0u; i < result_array.size(); ++i) {
        ASSERT_EQ(result_array[i], 0);
    }
}

TEST(DataStream, Int32ValueIsCorrectylyReadFromStream) {
    char data[] = {0x78, 0x56, 0x34, 0x12};
    cs::IDataStream stream(data, sizeof data);
    auto value = stream.parseValue<int32_t>();
    ASSERT_EQ(value, 0x12345678);
}

TEST(DataStream, Int32ValueIsCorrectylyWrittenToStream) {
    cs::Bytes bytes;
    cs::ODataStream stream(bytes);
    int32_t value = 0x12345678;
    stream.addValue<int32_t>(value);
    ASSERT_EQ(bytes, cs::Bytes({0x78, 0x56, 0x34, 0x12}));
}

TEST(DataStream, ByteArrayIsCorrectlyWrittenToStream) {
    cs::Bytes bytes;
    cs::ODataStream stream(bytes);
    cs::ByteArray<8> array = {0, 1, 2, 3, 4, 5, 5, 145};
    stream.addArray(array);
    ASSERT_EQ(bytes, cs::Bytes({0, 1, 2, 3, 4, 5, 5, 145}));
}

TEST(DataStream, ByteArrayIsCorrectlyReadFromStream) {
    char data[] = {0x78, 0x56, 0x34, 0x12};
    cs::IDataStream stream(data, sizeof data);
    auto value = stream.parseArray<char, 4>();
    ASSERT_EQ(value.size(), 4);
    ASSERT_EQ(value[0], 0x78);
    ASSERT_EQ(value[1], 0x56);
    ASSERT_EQ(value[2], 0x34);
    ASSERT_EQ(value[3], 0x12);
}

TEST(DataStream, MustGetZeroFilledByteArrayIfRequestedTooMany) {
    char data[] = {0x78, 0x56, 0x34, 0x12};
    cs::IDataStream stream(data, sizeof data);
    auto value = stream.parseArray<char, 5>();
    ASSERT_EQ(value.size(), 5);
    ASSERT_EQ(value[0], 0);
    ASSERT_EQ(value[1], 0);
    ASSERT_EQ(value[2], 0);
    ASSERT_EQ(value[3], 0);
    ASSERT_EQ(value[4], 0);
}

TEST(DataStream, CorrectlySkipsRequestedNumberOfBytes) {
    char data[8] = "1234567";
    cs::IDataStream stream(data, sizeof data);
    ASSERT_TRUE(stream.isAvailable(8));
    stream.skip<3>();
    ASSERT_TRUE(stream.isAvailable(5));
    stream.skip<4>();
    ASSERT_TRUE(stream.isAvailable(1));
    stream.skip<1>();
    ASSERT_FALSE(stream.isAvailable(1));
}

TEST(DataStream, CorrectValuesSerialization) {
    int expectedIntValue = 100;
    double expectedDoubleValue = 2.0;
    int64_t expectedInt64Value = 677777;
    constexpr size_t expectedSize = sizeof(expectedIntValue) + sizeof(expectedDoubleValue) + sizeof(expectedInt64Value);

    cs::Bytes bytes;
    cs::ODataStream stream(bytes);
    stream << expectedIntValue << expectedDoubleValue << expectedInt64Value;

    ASSERT_EQ(expectedSize, bytes.size());
    ASSERT_EQ(expectedSize, stream.size());

    cs::IDataStream readStream(bytes.data(), bytes.size());
    cs::Console::writeLine("Read data stream size before parsing: ", readStream.size());

    decltype(expectedIntValue) intValue = 0;
    decltype(expectedDoubleValue) doubleValue = 0;
    decltype(expectedInt64Value) int64Value = 0;

    readStream >> intValue >> doubleValue >> int64Value;
    cs::Console::writeLine("Read data stream size after parsing: ", readStream.size());

    ASSERT_FALSE(readStream.isAvailable(1));
    ASSERT_EQ(readStream.size(), 0);
    ASSERT_TRUE(readStream.isValid());

    ASSERT_EQ(intValue, expectedIntValue);
    ASSERT_EQ(static_cast<int>(doubleValue), static_cast<int>(expectedDoubleValue));
    ASSERT_EQ(int64Value, expectedInt64Value);
}

TEST(DataStream, CorrectAmountSerialization) {
    csdb::Amount amount{100};

    cs::Bytes bytes;
    cs::ODataStream stream(bytes);

    stream << amount;

    ASSERT_TRUE(stream.size() != 0);

    cs::IDataStream readStream(bytes.data(), bytes.size());
    csdb::Amount expectedAmount;

    readStream >> expectedAmount;

    ASSERT_TRUE(amount == expectedAmount);
}

TEST(DataStream, AvailabilityOfLargeSizes) {
    char data[16] = {};
    cs::IDataStream stream(data, sizeof data);
    stream.skip(8);
    ASSERT_TRUE(stream.isAvailable(8));
    ASSERT_FALSE(stream.isAvailable(9));
    ASSERT_FALSE(stream.isAvailable(std::numeric_limits<std::size_t>::max()));
    ASSERT_FALSE(stream.isAvailable(std::numeric_limits<std::size_t>::max() - 7));
}

TEST(DataStream, LargeLengthPrefixesAreRefused) {
    for (std::size_t length : {std::numeric_limits<std::size_t>::max(), std::numeric_limits<std::size_t>::max() - 7,
                               std::numeric_limits<std::size_t>::max() - 3}) {
        cs::Bytes bytes;
        cs::ODataStream out(bytes);
        out << length;
        bytes.resize(bytes.size() + 8, 0);

        cs::IDataStream text(bytes.data(), bytes.size());
        std::string s;
        text >> s;
        EXPECT_TRUE(s.empty());

        cs::IDataStream vector(bytes.data(), bytes.size());
        cs::Bytes v;
        vector >> v;
        EXPECT_TRUE(v.empty());

        cs::IDataStream sequences(bytes.data(), bytes.size());
        std::vector<uint64_t> list;
        sequences >> list;
        EXPECT_TRUE(list.empty());
    }
}

// the bound check against the obvious formula, for every position and the sizes around the edges
TEST(DataStream, AvailabilityAtEveryPosition) {
    const size_t max = std::numeric_limits<size_t>::max();
    for (size_t total : {size_t(0), size_t(1), size_t(16), size_t(4096)}) {
        std::vector<char> data(total + 1, 0);
        for (size_t pos = 0; pos <= total; ++pos) {
            cs::IDataStream stream(data.data(), total);
            stream.skip(pos);
            ASSERT_TRUE(stream.isValid());
            const size_t left = total - pos;
            for (size_t size : {size_t(0), size_t(1), left, left + 1, max - pos, max - pos + 1, max}) {
                EXPECT_EQ(stream.isAvailable(size), size <= left) << total << " " << pos << " " << size;
            }
        }
    }
}

TEST(DataStream, ExhaustedStream) {
    char data[8] = {};
    cs::IDataStream stream(data, sizeof data);
    stream.skip(8);
    EXPECT_TRUE(stream.isAvailable(0));
    EXPECT_FALSE(stream.isAvailable(1));
    EXPECT_FALSE(stream.isAvailable(std::numeric_limits<size_t>::max()));
    EXPECT_TRUE(stream.isEmpty());
    EXPECT_TRUE(stream.isValid());
}

TEST(DataStream, SkipBeyondTheEndKeepsThePosition) {
    char data[16] = {};
    cs::IDataStream stream(data, sizeof data);
    stream.skip(4);
    stream.skip(std::numeric_limits<size_t>::max() - 3);
    EXPECT_FALSE(stream.isValid());
    EXPECT_EQ(stream.size(), 12u);
}

TEST(DataStream, HonestEncodingsAtTheBoundary) {
    for (size_t extra : {size_t(0), size_t(1)}) {
        cs::Bytes bytes;
        cs::ODataStream out(bytes);
        out << std::string(100, 's');
        // a length that claims one byte more than there is
        if (extra) {
            const size_t claimed = 101;
            std::memcpy(bytes.data(), &claimed, sizeof claimed);
        }
        cs::IDataStream in(bytes.data(), bytes.size());
        std::string s;
        in >> s;
        if (extra) {
            EXPECT_FALSE(in.isValid());
            EXPECT_TRUE(s.empty());
        }
        else {
            EXPECT_TRUE(in.isValid());
            EXPECT_EQ(s, std::string(100, 's'));
            EXPECT_TRUE(in.isEmpty());
        }
    }
}

TEST(DataStream, AVectorCountBeyondTheData) {
    cs::Bytes bytes;
    cs::ODataStream out(bytes);
    out << size_t(1'000'000) << uint64_t(1) << uint64_t(2);
    cs::IDataStream in(bytes.data(), bytes.size());
    std::vector<cs::PublicKey> keys;
    in >> keys;
    EXPECT_TRUE(keys.empty());   // 1'000'000 keys of 32 bytes are not in 16 bytes
}

TEST(DataStream, BytesViewAndConvertReadFromThePosition) {
    cs::Bytes bytes;
    cs::ODataStream out(bytes);
    out << uint32_t(7) << cs::Bytes{1, 2, 3};
    {
        cs::IDataStream in(bytes.data(), bytes.size());
        uint32_t v = 0;
        in >> v;
        const auto view = in.parseBytesView();
        ASSERT_EQ(view.size(), 3u);
        EXPECT_EQ(view.data()[0], 1);
        EXPECT_TRUE(in.isEmpty());
    }
    {
        cs::IDataStream in(bytes.data(), bytes.size());
        uint32_t v = 0;
        in >> v;
        const auto rest = in.convert<cs::Bytes>();
        EXPECT_EQ(rest.size(), bytes.size() - sizeof(uint32_t));
        EXPECT_EQ(rest.front(), bytes[sizeof(uint32_t)]);
    }
}
