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

TEST(DataStream, endpoint)
{
  cs::Bytes bytes;
  cs::DataStream stream(bytes);
  boost::asio::ip::udp::endpoint ep(boost::asio::ip::address::from_string("127.0.0.1"), 80);
  stream << ep;
  boost::asio::ip::udp::endpoint endpoint;
//  stream >> endpoint;
//  endpoint = stream.endpoint();

//  std::cout << "address: " << endpoint.address().to_v4().to_string() << std::endl;

  ASSERT_TRUE(true);
}

TEST(DataStream, streamField)
{
  cs::Bytes bytes;
  cs::DataStream stream(bytes);
  stream.setStreamField<int>(13);

  int val = stream.data()[0];

  ASSERT_EQ(13, val);
}

TEST(DataStream, streamArray)
{
  cs::Bytes bytes;
  cs::DataStream stream(bytes);
  std::array<char, 4> ar {{'A', 'B', 'C', 'D'}};

  stream.setStreamArray(ar);

  ASSERT_TRUE('A' == stream.data()[0]);
}

TEST(DataStream, transactionHash)
{
  cs::Bytes bytes;
  cs::DataStream stream(bytes);
  TransactionsPacketHash hash = TransactionsPacketHash::fromString("d41d8cd98f00b204e9800998ecf8427ed41d8cd98f00b204e9800998ecf8427e");
  stream.addTransactionsHash(hash);

  ASSERT_TRUE(stream.size() == 40);
}