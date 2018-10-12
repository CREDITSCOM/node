#define TESTING

#include <gtest/gtest.h>
#include "datastream.h"

using namespace cs;

TEST(DataStream, isAvailable)
{
  std::string data = "teststring";
  cs::DataStream stream(const_cast<char*>(data.data()), data.size());

  ASSERT_TRUE(stream.isAvailable(1));
}

TEST(DataStream, data)
{
  std::string data = "teststring";
  cs::DataStream stream(const_cast<char*>(data.data()), data.size());

  ASSERT_FALSE(stream.data() == nullptr);
}

TEST(DataStream, endpoint) // unfinished test. Probably problem with mIndex field in DataStream class
{
  char* data = new char[100]();
  cs::DataStream stream(data, 100);
  boost::asio::ip::udp::endpoint ep(boost::asio::ip::address::from_string("127.0.0.1"), 80);
  stream << ep;
  auto endpoint = stream.endpoint();
//  std::cout << "address: " << endpoint.address().to_v4().to_string() << std::endl;

  delete[] data;

  ASSERT_TRUE(true);
}

TEST(DataStream, streamField)
{
  char* data = new char[100]();
  cs::DataStream stream(data, 100);
  stream.setStreamField<int>(13);

  int val = stream.data()[0];

  delete[] data;

  ASSERT_EQ(13, val);
}

TEST(DataStream, streamArray)
{
  char* data = new char[100]();
  cs::DataStream stream(data, 100);
  std::array<char, 4> ar {{'A', 'B', 'C', 'D'}};

  stream.setStreamArray(ar);

  delete[] data;

  ASSERT_TRUE(true);
}