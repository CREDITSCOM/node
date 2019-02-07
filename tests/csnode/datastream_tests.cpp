#include <gtest/gtest.h>
#include "datastream.hpp"

TEST(DataStream, DataPointerIsCorrectAfterCreation) {
  char data[8] = "1234567";
  cs::DataStream stream(data, sizeof data);
  ASSERT_EQ(stream.data(), data);
}

TEST(DataStream, SizeIsCorrect) {
  char data[8] = "1234567";
  cs::DataStream stream(data, sizeof data);
  ASSERT_EQ(stream.size(), sizeof data);
}

TEST(DataStream, IsValidAfterCreation) {
  char data[8] = "1234567";
  cs::DataStream stream(data, sizeof data);
  ASSERT_TRUE(stream.isValid());
}

TEST(DataStream, IsAvailableReturnsTrueIfRequestedEnough) {
  char data[8] = "1234567";
  cs::DataStream stream(data, sizeof data);
  ASSERT_TRUE(stream.isAvailable(8));
}

TEST(DataStream, IsAvailableReturnsTrueIfRequestedZero) {
  char data[8] = "1234567";
  cs::DataStream stream(data, sizeof data);
  ASSERT_TRUE(stream.isAvailable(0));
}

TEST(DataStream, IsAvailableReturnsFalseIfRequestedTooMany) {
  char data[8] = "1234567";
  cs::DataStream stream(data, sizeof data);
  ASSERT_FALSE(stream.isAvailable(9));
}

TEST(DataStream, MustGetCorrectStdArrayFromStream) {
  char data[8] = "1234567";
  cs::DataStream stream(data, sizeof data);
  auto result_array = stream.parseArray<char, sizeof data>();
  ASSERT_EQ(result_array[7], '\0');
  ASSERT_EQ(8, result_array.size());
  std::array<char, 8> test_array = {'1', '2', '3', '4', '5', '6', '7', '\0'};
  ASSERT_EQ(test_array, result_array);
}

TEST(DataStream, MustGetZeroFilledStdArrayIfRequestedMoreThanAvailable) {
  char data[8] = "1234567";
  cs::DataStream stream(data, sizeof data);
  auto result_array = stream.parseArray<char, 10>();
  ASSERT_EQ(result_array.size(), 10);
  for (auto i = 0u; i < result_array.size(); ++i) {
    ASSERT_EQ(result_array[i], 0);
  }
}

TEST(DataStream, EndPointWithIp4AndPortIsCorrectlyWritrenToStream) {
  cs::Bytes bytes;
  cs::DataStream stream(bytes);
  boost::asio::ip::udp::endpoint endpoint(
      boost::asio::ip::address::from_string("127.0.0.1"), 80);
  stream.addEndpoint(endpoint);
  const char encoded[7] = {0x06, 0x7f, 0x00, 0x00, 0x01, 0x50, 0x00};
  ASSERT_EQ(stream.size(), 7);
  ASSERT_TRUE(0 == memcmp(encoded, stream.data(), sizeof encoded));
}

TEST(DataStream, EndPointWithIp4AndPortIsCorrectlyReadFromStream) {
  char data[] = {0x06, 0x7f, 0x00, 0x00, 0x01, 0x50, 0x00};
  cs::DataStream stream(data, sizeof data);
  boost::asio::ip::udp::endpoint endpoint = stream.parseEndpoint();
  ASSERT_EQ(endpoint.address(),
            boost::asio::ip::address::from_string("127.0.0.1"));
  ASSERT_EQ(endpoint.port(), 80);
}

TEST(DataStream, EmptyEndPointIsCorrectlyReadFromStream) {
  char data[] = {0x00};
  cs::DataStream stream(data, sizeof data);
  boost::asio::ip::udp::endpoint endpoint = stream.parseEndpoint();
  ASSERT_EQ(endpoint.address(), boost::asio::ip::address());
  ASSERT_EQ(endpoint.port(), 0);
}

TEST(DataStream, EndPointWithOnlyIp4IsCorrectlyReadFromStream) {
  char data[] = {0x02, 0x7f, 0x00, 0x00, 0x01, 0x00};
  cs::DataStream stream(data, sizeof data);
  boost::asio::ip::udp::endpoint endpoint = stream.parseEndpoint();
  ASSERT_EQ(endpoint.address(),
            boost::asio::ip::address::from_string("127.0.0.1"));
  ASSERT_EQ(endpoint.port(), 0);
}

TEST(DataStream, EndPointWithOnlyPortIsCorrectlyReadFromStream) {
  char data[] = {0x04, 0x7f, 0x00};
  cs::DataStream stream(data, sizeof data);
  boost::asio::ip::udp::endpoint endpoint = stream.parseEndpoint();
  ASSERT_EQ(endpoint.address(), boost::asio::ip::address());
  ASSERT_EQ(endpoint.port(), 127);
}

TEST(DataStream, Int32ValueIsCorrectylyReadFromStream) {
  char data[] = {0x78, 0x56, 0x34, 0x12};
  cs::DataStream stream(data, sizeof data);
  auto value = stream.parseValue<int32_t>();
  ASSERT_EQ(value, 0x12345678);
}

TEST(DataStream, Int32ValueIsCorrectylyWrittenToStream) {
  cs::Bytes bytes;
  cs::DataStream stream(bytes);
  int32_t value = 0x12345678;
  stream.addValue<int32_t>(value);
  ASSERT_EQ(bytes, cs::Bytes({0x78, 0x56, 0x34, 0x12}));
}

TEST(DataStream, ByteArrayIsCorrectlyWrittenToStream) {
  cs::Bytes bytes;
  cs::DataStream stream(bytes);
  cs::ByteArray<8> array = {0, 1, 2, 3, 4, 5, 5, 145};
  stream.addArray(array);
  ASSERT_EQ(bytes, cs::Bytes({0, 1, 2, 3, 4, 5, 5, 145}));
}

TEST(DataStream, ByteArrayIsCorrectlyReadFromStream) {
  char data[] = {0x78, 0x56, 0x34, 0x12};
  cs::DataStream stream(data, sizeof data);
  auto value = stream.parseArray<char, 4>();
  ASSERT_EQ(value.size(), 4);
  ASSERT_EQ(value[0], 0x78);
  ASSERT_EQ(value[1], 0x56);
  ASSERT_EQ(value[2], 0x34);
  ASSERT_EQ(value[3], 0x12);
}

TEST(DataStream, MustGetZeroFilledByteArrayIfRequestedTooMany) {
  char data[] = {0x78, 0x56, 0x34, 0x12};
  cs::DataStream stream(data, sizeof data);
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
  cs::DataStream stream(data, sizeof data);
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
  cs::DataStream stream(bytes);
  stream << expectedIntValue << expectedDoubleValue << expectedInt64Value;

  ASSERT_EQ(expectedSize, bytes.size());
  ASSERT_EQ(expectedSize, stream.size());

  cs::DataStream readStream(bytes.data(), bytes.size());
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
