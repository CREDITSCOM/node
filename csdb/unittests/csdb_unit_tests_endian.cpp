#include "csdb/internal/endian.h"

#include <cstring>

#include <gtest/gtest.h>

namespace {
  typedef ::testing::Types<
    int8_t, uint8_t,
    int16_t, uint16_t,
    int32_t, uint32_t,
    int64_t, uint64_t,
    intmax_t, uintmax_t
  > TypesForTest;
}

template <typename T>
class EndianTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    uint8_t buf_l[sizeof(Type)];
    uint8_t buf_b[sizeof(Type)];
    platform_endian_ = 0;
    for (uint8_t i = 0; i < sizeof(Type); ++i) {
      buf_l[sizeof(Type) - i - 1] = buf_b[i] = (i + 1);
      platform_endian_ = static_cast<Type>((platform_endian_ * 256) + (i + 1));
    }
    memcpy(&little_endian_, buf_l, sizeof(Type));
    memcpy(&big_endian_, buf_b, sizeof(Type));
  }

  typedef T Type;
  Type little_endian_;
  Type big_endian_;
  Type platform_endian_;
};

TYPED_TEST_CASE(EndianTest, TypesForTest);

TYPED_TEST(EndianTest, ReverseByteOrder)
{
  EXPECT_EQ(this->little_endian_, ::csdb::internal::reverse_byte_order(this->big_endian_));
  EXPECT_EQ(this->big_endian_, ::csdb::internal::reverse_byte_order(this->little_endian_));
}

TYPED_TEST(EndianTest, EndianConvertes)
{
  EXPECT_EQ(this->little_endian_, ::csdb::internal::to_little_endian(this->platform_endian_));
  EXPECT_EQ(this->platform_endian_, ::csdb::internal::from_little_endian(this->little_endian_));

  EXPECT_EQ(this->big_endian_, ::csdb::internal::to_big_endian(this->platform_endian_));
  EXPECT_EQ(this->platform_endian_, ::csdb::internal::from_big_endian(this->big_endian_));
}
