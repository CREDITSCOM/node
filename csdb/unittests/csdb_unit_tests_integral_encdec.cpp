#include "integral_encdec.h"

#include <limits>

#include <gtest/gtest.h>

class IntegralEncDec : public ::testing::Test
{
};

#define string_literal(s) std::string(s, sizeof(s) - 1)
#define TEST_ENCDEC(name, type, value, encoded) \
  TEST_F(IntegralEncDec, name) \
  { \
    char buf[::csdb::priv::MAX_INTEGRAL_ENCODED_SIZE];\
    type v1 = value, v2; \
    std::size_t size = ::csdb::priv::encode(buf, v1); \
    EXPECT_EQ(std::string(buf, size), string_literal(encoded)); \
    EXPECT_EQ(::csdb::priv::decode(buf, size, v2), size); \
    EXPECT_EQ(v1, v2); \
    EXPECT_EQ(::csdb::priv::decode(buf, size - 1, v2), 0); \
  }

TEST_ENCDEC(BoolTrue, bool, true, "\x01")
TEST_ENCDEC(BoolFalse, bool, false, "\x00")

// int8_t
TEST_ENCDEC(int8_t_0, int8_t, 0, "\x00")
TEST_ENCDEC(int8_t_1p, int8_t, 1, "\x02")
TEST_ENCDEC(int8_t_1m, int8_t, -1, "\xFE")
TEST_ENCDEC(int8_t_min, int8_t, std::numeric_limits<int8_t>::min(), "\x01\xFE")
TEST_ENCDEC(int8_t_min_half, int8_t, std::numeric_limits<int8_t>::min() / 2, "\x80")
TEST_ENCDEC(int8_t_max, int8_t, std::numeric_limits<int8_t>::max(), "\xFD\x01")

// uint8_t
TEST_ENCDEC(uint8_t_0, uint8_t, 0, "\x00")
TEST_ENCDEC(uint8_t_1p, uint8_t, 1, "\x02")
TEST_ENCDEC(uint8_t_max, uint8_t, std::numeric_limits<uint8_t>::max(), "\xFD\x03")

// int16_t
TEST_ENCDEC(int16_t_0, int16_t, 0, "\x00")
TEST_ENCDEC(int16_t_1p, int16_t, 1, "\x02")
TEST_ENCDEC(int16_t_1m, int16_t, -1, "\xFE")
TEST_ENCDEC(int16_t_min, int16_t, std::numeric_limits<int16_t>::min(), "\x03\x00\xFC")
TEST_ENCDEC(int16_t_min_d4, int16_t, std::numeric_limits<int16_t>::min() / 4, "\x01\x80")
TEST_ENCDEC(int16_t_max, int16_t, std::numeric_limits<int16_t>::max(), "\xFB\xFF\x03")

// uint16_t
TEST_ENCDEC(uint16_t_0, uint16_t, 0, "\x00")
TEST_ENCDEC(uint16_t_1p, uint16_t, 1, "\x02")
TEST_ENCDEC(uint16_t_max, uint16_t, std::numeric_limits<uint16_t>::max(), "\xFB\xFF\x07")

// int32_t
TEST_ENCDEC(int32_t_0, int32_t, 0, "\x00")
TEST_ENCDEC(int32_t_1p, int32_t, 1, "\x02")
TEST_ENCDEC(int32_t_1m, int32_t, -1, "\xFE")
TEST_ENCDEC(int32_t_min, int32_t, std::numeric_limits<int32_t>::min(), "\x0F\x00\x00\x00\xF0")
TEST_ENCDEC(int32_t_min_d16, int32_t, std::numeric_limits<int32_t>::min() / 16, "\x07\x00\x00\x80")
TEST_ENCDEC(int32_t_max, int32_t, std::numeric_limits<int32_t>::max(), "\xEF\xFF\xFF\xFF\x0F")

// uint32_t
TEST_ENCDEC(uint32_t_0, uint32_t, 0, "\x00")
TEST_ENCDEC(uint32_t_1p, uint32_t, 1, "\x02")
TEST_ENCDEC(uint32_t_max, uint32_t, std::numeric_limits<uint32_t>::max(), "\xEF\xFF\xFF\xFF\x1F")

// int64_t
TEST_ENCDEC(int64_t_0, int64_t, 0, "\x00")
TEST_ENCDEC(int64_t_1p, int64_t, 1, "\x02")
TEST_ENCDEC(int64_t_1m, int64_t, -1, "\xFE")
TEST_ENCDEC(int64_t_min, int64_t, std::numeric_limits<int64_t>::min(), "\xFF\x00\x00\x00\x00\x00\x00\x00\x80")
TEST_ENCDEC(int64_t_min_d128, int64_t, std::numeric_limits<int64_t>::min() / 128, "\xFF\x00\x00\x00\x00\x00\x00\x00\xFF")
TEST_ENCDEC(int64_t_min_d256, int64_t, std::numeric_limits<int64_t>::min() / 256, "\x7F\x00\x00\x00\x00\x00\x00\x80")
TEST_ENCDEC(int64_t_max, int64_t, std::numeric_limits<int64_t>::max(), "\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x7F")

// uint64_t
TEST_ENCDEC(uint64_t_0, uint64_t, 0, "\x00")
TEST_ENCDEC(uint64_t_1p, uint64_t, 1, "\x02")
// std::numeric_limits<uint64_t>::max() то же самое, что (-1), так добавим дополнительный тест
TEST_ENCDEC(uint64_t_max, uint64_t, std::numeric_limits<uint64_t>::max(), "\xFE")
TEST_ENCDEC(uint64_t_max_1m, uint64_t, std::numeric_limits<uint64_t>::max() >> 2, "\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x3F")

// enum
TEST_F(IntegralEncDec, Enum)
{
  char buf[::csdb::priv::MAX_INTEGRAL_ENCODED_SIZE];
  enum Enum {One, Two, Three};
  Enum v1 = Two, v2;
  std::size_t size = ::csdb::priv::encode(buf, v1);
  EXPECT_EQ(::csdb::priv::decode(buf, size, v2), size);
  EXPECT_EQ(v1, v2);
  EXPECT_EQ(::csdb::priv::decode(buf, size - 1, v2), 0);
}
