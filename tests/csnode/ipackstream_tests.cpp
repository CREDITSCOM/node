#define TESTING

#include <gtest/gtest.h>
#include "packstream.hpp"

TEST(IPackStream, init)
{
  IPackStream iPackStream;

  ASSERT_FALSE(iPackStream.good());

  uint8_t data[] = {1, 2, 3, 4};
  iPackStream.init(data, 4);

  ASSERT_TRUE(iPackStream.good());
}

TEST(IPackStream, canPeek)
{
  IPackStream iPackStream;
  uint8_t data[] = {1, 2, 3, 4};
  iPackStream.init(data, 4);

  ASSERT_TRUE(iPackStream.canPeek<uint8_t>());
  ASSERT_FALSE(iPackStream.canPeek<uint64_t>());
}

TEST(IPackStream, peek)
{
  IPackStream iPackStream;
  uint8_t data[] = {11, 12, 13, 14};
  iPackStream.init(data, 4);

  ASSERT_EQ(11, (int)iPackStream.peek<uint8_t>());
}

TEST(IPackStream, skip)
{
  IPackStream iPackStream;
  uint8_t data[] = {11, 12, 13, 14};
  iPackStream.init(data, 4);
  iPackStream.skip<uint8_t>();

  ASSERT_EQ(12, (int)iPackStream.peek<uint8_t>());
}

TEST(IPackStream, saveSkip)
{
  IPackStream iPackStream;
  uint8_t data[] = {11, 12, 13};
  iPackStream.init(data, 3);
  iPackStream.safeSkip<uint32_t>();

  ASSERT_FALSE(iPackStream.good());

  iPackStream.safeSkip<uint8_t>(2);

  ASSERT_EQ(13, (int)iPackStream.peek<uint8_t>());
}

TEST(IPackStream, input)
{
  IPackStream iPackStream;
  uint8_t data[] = {11, 12, 13, 14, 15, 16, 17};
  iPackStream.init(data, 7);

  uint8_t val;
  iPackStream >> val;

  ASSERT_EQ(11, (int)val);

  FixedString<2> str;
  iPackStream >> str;

  ASSERT_EQ(12, (int)str.data()[0]);

  cs::ByteArray<2> array;
  iPackStream >> array;

  ASSERT_EQ(14, (int)array.data()[0]);
}

TEST(IPackStream, end)
{
  IPackStream iPackStream;
  uint8_t data[] = {11, 12};
  iPackStream.init(data, 2);

  ASSERT_FALSE(iPackStream.end());

  iPackStream.skip<uint8_t>();
  iPackStream.skip<uint8_t>();

  ASSERT_TRUE(iPackStream.end());
}

TEST(IPackStream, getCurrPtr)
{
  IPackStream iPackStream;
  uint8_t data[] = {11, 12};
  iPackStream.init(data, 2);

  ASSERT_EQ(11, (int)*iPackStream.getCurrPtr());
}
