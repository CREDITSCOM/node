#include "binary_streams.h"
#include <limits>
#include <gtest/gtest.h>

using namespace ::csdb::priv;

class BinaryStreams : public ::testing::Test
{
protected:
  struct Test
  {
    int i;
    bool b;
    std::string s;
    inline void put(obstream& os) const
    {
      os.put(i);
      os.put(b);
      os.put(s);
    }

    inline bool get(ibstream& is)
    {
      return is.get(i)
          && is.get(b)
          && is.get(s);
    }

    bool operator ==(const Test& other) const
    {
      return (i == other.i)
          && (b == other.b)
          && (s == other.s);
    }
  };
};

inline ::csdb::internal::byte_array from_string(const std::string &src)
{
  return ::csdb::internal::byte_array(src.begin(), src.end());
}

#define string_literal(s) std::string(s, sizeof(s) - 1)
#define TEST_BS_INT(name, type, value, encoded) \
  TEST_F(BinaryStreams, name) \
  { \
    obstream o; \
    type v1 = value; \
    o.put(v1); \
    const ::csdb::internal::byte_array &s = o.buffer(); \
    EXPECT_EQ(s, from_string(string_literal(encoded))); \
    ibstream i(s); \
    type v2; \
    EXPECT_TRUE(i.get(v2)); \
    EXPECT_TRUE(i.empty()); \
    EXPECT_EQ(v1, v2); \
    ::csdb::internal::byte_array s1 = s; \
    EXPECT_FALSE(s1.empty()); \
    s1.resize(s1.size() - 1); \
    ibstream i1(s1); \
    type v3; \
    EXPECT_FALSE(i1.get(v3)); \
  }

TEST_BS_INT(BoolTrue, bool, true, "\x01")
TEST_BS_INT(BoolFalse, bool, false, "\x00")

// int8_t
TEST_BS_INT(int8_t_0, int8_t, 0, "\x00")
TEST_BS_INT(int8_t_1p, int8_t, 1, "\x02")
TEST_BS_INT(int8_t_1m, int8_t, -1, "\xFE")
TEST_BS_INT(int8_t_min, int8_t, std::numeric_limits<int8_t>::min(), "\x01\xFE")
TEST_BS_INT(int8_t_min_half, int8_t, std::numeric_limits<int8_t>::min() / 2, "\x80")
TEST_BS_INT(int8_t_max, int8_t, std::numeric_limits<int8_t>::max(), "\xFD\x01")

// uint8_t
TEST_BS_INT(uint8_t_0, uint8_t, 0, "\x00")
TEST_BS_INT(uint8_t_1p, uint8_t, 1, "\x02")
TEST_BS_INT(uint8_t_max, uint8_t, std::numeric_limits<uint8_t>::max(), "\xFD\x03")

// int16_t
TEST_BS_INT(int16_t_0, int16_t, 0, "\x00")
TEST_BS_INT(int16_t_1p, int16_t, 1, "\x02")
TEST_BS_INT(int16_t_1m, int16_t, -1, "\xFE")
TEST_BS_INT(int16_t_min, int16_t, std::numeric_limits<int16_t>::min(), "\x03\x00\xFC")
TEST_BS_INT(int16_t_min_d4, int16_t, std::numeric_limits<int16_t>::min() / 4, "\x01\x80")
TEST_BS_INT(int16_t_max, int16_t, std::numeric_limits<int16_t>::max(), "\xFB\xFF\x03")

// uint16_t
TEST_BS_INT(uint16_t_0, uint16_t, 0, "\x00")
TEST_BS_INT(uint16_t_1p, uint16_t, 1, "\x02")
TEST_BS_INT(uint16_t_max, uint16_t, std::numeric_limits<uint16_t>::max(), "\xFB\xFF\x07")

// int32_t
TEST_BS_INT(int32_t_0, int32_t, 0, "\x00")
TEST_BS_INT(int32_t_1p, int32_t, 1, "\x02")
TEST_BS_INT(int32_t_1m, int32_t, -1, "\xFE")
TEST_BS_INT(int32_t_min, int32_t, std::numeric_limits<int32_t>::min(), "\x0F\x00\x00\x00\xF0")
TEST_BS_INT(int32_t_min_d16, int32_t, std::numeric_limits<int32_t>::min() / 16, "\x07\x00\x00\x80")
TEST_BS_INT(int32_t_max, int32_t, std::numeric_limits<int32_t>::max(), "\xEF\xFF\xFF\xFF\x0F")

// uint32_t
TEST_BS_INT(uint32_t_0, uint32_t, 0, "\x00")
TEST_BS_INT(uint32_t_1p, uint32_t, 1, "\x02")
TEST_BS_INT(uint32_t_max, uint32_t, std::numeric_limits<uint32_t>::max(), "\xEF\xFF\xFF\xFF\x1F")

// int64_t
TEST_BS_INT(int64_t_0, int64_t, 0, "\x00")
TEST_BS_INT(int64_t_1p, int64_t, 1, "\x02")
TEST_BS_INT(int64_t_1m, int64_t, -1, "\xFE")
TEST_BS_INT(int64_t_min, int64_t, std::numeric_limits<int64_t>::min(), "\xFF\x00\x00\x00\x00\x00\x00\x00\x80")
TEST_BS_INT(int64_t_min_d128, int64_t, std::numeric_limits<int64_t>::min() / 128, "\xFF\x00\x00\x00\x00\x00\x00\x00\xFF")
TEST_BS_INT(int64_t_min_d256, int64_t, std::numeric_limits<int64_t>::min() / 256, "\x7F\x00\x00\x00\x00\x00\x00\x80")
TEST_BS_INT(int64_t_max, int64_t, std::numeric_limits<int64_t>::max(), "\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x7F")

// uint64_t
TEST_BS_INT(uint64_t_0, uint64_t, 0, "\x00")
TEST_BS_INT(uint64_t_1p, uint64_t, 1, "\x02")
// std::numeric_limits<uint64_t>::max() то же самое, что (-1), так добавим дополнительный тест
TEST_BS_INT(uint64_t_max, uint64_t, std::numeric_limits<uint64_t>::max(), "\xFE")
TEST_BS_INT(uint64_t_max_1m, uint64_t, std::numeric_limits<uint64_t>::max() >> 2, "\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x3F")

#define TEST_BS_STRING(name, value, encoded) \
  TEST_F(BinaryStreams, String##name) \
  { \
    obstream o; \
    std::string v1 = value; \
    o.put(v1); \
    const ::csdb::internal::byte_array &s = o.buffer(); \
    EXPECT_EQ(s, from_string(string_literal(encoded))); \
    ibstream i(s); \
    std::string v2; \
    EXPECT_TRUE(i.get(v2)); \
    EXPECT_EQ(v1, v2); \
  }

TEST_BS_STRING(Empty, std::string(), "\x00")
TEST_BS_STRING(Simple, string_literal("Test string"), "\x16Test string")
TEST_BS_STRING(WithZero, string_literal("Test\0string"), "\x16Test\0string")

TEST_F(BinaryStreams, ManyValues)
{
  bool b1 = true;
  long l1 = 1000;
  std::string s1("Test string");
  int i1 = 20;

  obstream o;
  o.put(b1);
  o.put(l1);
  o.put(s1);
  o.put(i1);

  const ::csdb::internal::byte_array &s = o.buffer();
  EXPECT_EQ(s, from_string(string_literal("\x01\xA1\x0F\x16Test string\x28")));

  bool b2;
  long l2;
  std::string s2;
  int i2;

  ibstream i(s);
  EXPECT_TRUE(i.get(b2));
  EXPECT_TRUE(i.get(l2));
  EXPECT_TRUE(i.get(s2));
  EXPECT_TRUE(i.get(i2));

  EXPECT_EQ(b1, b2);
  EXPECT_EQ(l1, l2);
  EXPECT_EQ(s1, s2);
  EXPECT_EQ(i1, i2);
}

TEST_F(BinaryStreams, Enum)
{
  enum Enum {One, Two, Three};
  Enum v1 = Two;

  obstream o;
  o.put(v1);

  Enum v2 = One;
  ibstream i(o.buffer());
  EXPECT_TRUE(i.get(v2));
  EXPECT_TRUE(i.empty());
  EXPECT_EQ(v1, v2);
}

TEST_F(BinaryStreams, Struct1)
{
  Test v1{2, true, "Test string"};
  obstream o;
  o.put(v1);

  const ::csdb::internal::byte_array &s = o.buffer();
  EXPECT_EQ(s, from_string(string_literal("\x04\x01\x16Test string")));

  Test v2;
  ibstream i(s);
  EXPECT_TRUE(i.get(v2));
  EXPECT_TRUE(i.empty());
  EXPECT_EQ(v1, v2);
}

TEST_F(BinaryStreams, Struct2)
{
  Test v1{100, false, string_literal("Test\0string")};
  obstream o;
  o.put(v1);

  const ::csdb::internal::byte_array &s = o.buffer();
  EXPECT_EQ(s, from_string(string_literal("\x91\x01\x00\x16Test\x00string")));

  Test v2;
  ibstream i(s);
  EXPECT_TRUE(i.get(v2));
  EXPECT_TRUE(i.empty());
  EXPECT_EQ(v1, v2);
}

TEST_F(BinaryStreams, MapEmpty)
{
  ::std::map<int, ::std::string> v1;
  obstream o;
  o.put(v1);

  const ::csdb::internal::byte_array &s = o.buffer();
  EXPECT_EQ(s, from_string(string_literal("\0")));

  ::std::map<int, ::std::string> v2;
  ibstream i(s);
  EXPECT_TRUE(i.get(v2));
  EXPECT_TRUE(i.empty());
  EXPECT_EQ(v1, v2);
}

TEST_F(BinaryStreams, MapFilled)
{
  ::std::map<int, ::std::string> v1;
  v1.emplace(1, "Key1");
  v1.emplace(2, "Key2");
  v1.emplace(3, "Key3");
  obstream o;
  o.put(v1);

  const ::csdb::internal::byte_array &s = o.buffer();
  EXPECT_EQ(s, from_string(string_literal("\x06\x02\x08Key1\x04\x08Key2\x06\x08Key3")));

  ::std::map<int, ::std::string> v2;
  ibstream i(s);
  EXPECT_TRUE(i.get(v2));
  EXPECT_TRUE(i.empty());
  EXPECT_EQ(v1, v2);
}
