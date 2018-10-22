#define TESTING

#include <gtest/gtest.h>
#include "packstream.hpp"

class TestOPackStream : public ::testing::Test
{
protected:

  void SetUp() override
  {
    srand(time(nullptr));
    for (int i = 0; i < 32; ++i) {
      *(key_.data() + i) = static_cast<cs::Byte>((char) (rand() % 255));
    }
  }

  void TearDown() override {}

  cs::PublicKey key_;
};

void displayStreamData(OPackStream& stream)
{
  auto ptr = stream.getCurrPtr();
  auto offset = stream.getCurrSize();

  for(int i = 0; i < offset; i++){
    std::cout << "item: " << (int)(*(ptr - offset + i)) << std::endl;
  }
}

TEST_F(TestOPackStream, init)
{
  RegionAllocator allocator(100, 1);
  OPackStream oPackStream(&allocator, key_);

  oPackStream.init(BaseFlags::Fragmented | BaseFlags::NetworkMsg);
  auto ptr = oPackStream.getCurrPtr();

  ASSERT_EQ(1, (int)(*(ptr - 2)));

  oPackStream.init(BaseFlags::Fragmented);
  ptr = oPackStream.getCurrPtr();

  ASSERT_EQ((int)key_[key_.size() - 1], (int)(*(ptr - 1)));
}

TEST_F(TestOPackStream, clear)
{
  RegionAllocator allocator(100, 1);
  OPackStream oPackStream(&allocator, key_);

  oPackStream.init(BaseFlags::Fragmented);
  oPackStream.clear();

  ASSERT_EQ(0, oPackStream.getPacketsCount());
}

TEST_F(TestOPackStream, output) // TODO add transaction
{
  RegionAllocator allocator(200, 1);
  OPackStream oPackStream(&allocator, key_);

  oPackStream.init(BaseFlags::Fragmented | BaseFlags::NetworkMsg);
  uint8_t val = 13;
  oPackStream << val;

  ASSERT_EQ(13, (int)(*(oPackStream.getCurrPtr() - 1)));

  FixedString<3> str("str");
  oPackStream << str;

  ASSERT_EQ(114, (int)(*(oPackStream.getCurrPtr() - 1)));

  cs::ByteArray<2> array {{10, 11}};
  oPackStream << array;

  ASSERT_EQ(11, (int)(*(oPackStream.getCurrPtr() - 1)));

  auto adderss = boost::asio::ip::address_v4::from_string("127.0.0.1");
  oPackStream << adderss;

  ASSERT_EQ(1, (int)(*(oPackStream.getCurrPtr() - 1)));

  std::string abc("abc");
  oPackStream << abc;

  ASSERT_EQ(99, (int)(*(oPackStream.getCurrPtr() - 1)));

  csdb::internal::byte_array byteArray {{11, 12, 13}};
  auto poolHash = csdb::PoolHash::calc_from_data(byteArray);
  csdb::Pool pool(poolHash, (uint64_t)34);
  oPackStream << pool;

  ASSERT_TRUE(pool.is_valid());

  cs::Bytes bytes {{14, 15}};
  oPackStream << bytes;

  ASSERT_EQ(15, (int)(*(oPackStream.getCurrPtr() - 1)));
}

TEST_F(TestOPackStream, getPacketsCount)
{
  RegionAllocator allocator(100, 1);
  OPackStream oPackStream(&allocator, key_);

  oPackStream.init(BaseFlags::Fragmented | BaseFlags::NetworkMsg);

  ASSERT_EQ(1, oPackStream.getPacketsCount());
}

TEST_F(TestOPackStream, getCurrPtr)
{
  RegionAllocator allocator(100, 1);
  OPackStream oPackStream(&allocator, key_);

  oPackStream.init(BaseFlags::Fragmented | BaseFlags::NetworkMsg);

  ASSERT_EQ(1, (int)(*(oPackStream.getCurrPtr() - 2)));
}

TEST_F(TestOPackStream, getCurrSize)
{
  RegionAllocator allocator(100, 1);
  OPackStream oPackStream(&allocator, key_);

  oPackStream.init(BaseFlags::Fragmented | BaseFlags::NetworkMsg);

  ASSERT_EQ(5, oPackStream.getCurrSize());
}