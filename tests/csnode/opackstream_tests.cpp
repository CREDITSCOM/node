
//#define TESTING

#include <gtest/gtest.h>
#include "csdb/address.h"
#include "packstream.hpp"

const cs::PublicKey kPublicKey = {
    0x53, 0x4b, 0xd3, 0xdf, 0x77, 0x29, 0xfd, 0xcf, 0xea, 0x4a, 0xcd,
    0x0e, 0xcc, 0x14, 0xaa, 0x05, 0x0b, 0x77, 0x11, 0x6d, 0x8f, 0xcd,
    0x80, 0x4b, 0x45, 0x36, 0x6b, 0x5c, 0xae, 0x4a, 0x06, 0x82};

void DisplayRawData(const void* data, const size_t size) {
  std::cout << "data = {";
  for (auto i = 0u; i < size; i++) {
    std::cout << "0x" << std::hex << std::setw(2) << std::setfill('0')
              << (int)((uint8_t*)data)[i] << ", ";
  }
  std::cout << "}" << std::endl;
}

void displayStreamData(cs::OPackStream& stream) {
  auto ptr = stream.getCurrentPtr();
  auto offset = stream.getCurrentSize();
  DisplayRawData(ptr - offset, offset);
}

auto GetStreamData(cs::OPackStream& stream) {
  auto packets = stream.getPackets();
  auto buffer_data =
      std::make_unique<char>(static_cast<size_t>(Packet::MaxSize));
  boost::asio::mutable_buffer buffer(buffer_data.get(), Packet::MaxSize);
  auto encoded = packets->encode(buffer);
  return encoded;
}

const std::size_t kPageSizeForAllocator = 1000;  // 109 is minimal stable

TEST(OPackStream, InitializationWithFragmentedAndNetworkMsgFlags) {
  RegionAllocator allocator(kPageSizeForAllocator, 1);
  cs::OPackStream stream(&allocator, kPublicKey);
  const auto flags = BaseFlags(BaseFlags::Fragmented | BaseFlags::NetworkMsg);
  stream.init(flags);
  auto encoded = GetStreamData(stream);
  const unsigned char encoded_expected[] = {flags, 0x00, 0x00, 0x01, 0x00};
  ASSERT_EQ(encoded.size(), sizeof encoded_expected);
  ASSERT_TRUE(0 == memcmp(encoded.data(), encoded_expected, encoded.size()));
}

TEST(OPackStream, InitializationWithFragmentedFlagOnly) {
  RegionAllocator allocator(kPageSizeForAllocator, 1);
  cs::OPackStream stream(&allocator, kPublicKey);
  const auto flags = BaseFlags::Fragmented;
  stream.init(flags);
  auto encoded = GetStreamData(stream);
  const unsigned char encoded_expected[] = {
      flags, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00,  0x53, 0x4b, 0xd3, 0xdf, 0x77, 0x29, 0xfd, 0xcf, 0xea, 0x4a, 0xcd,
      0x0e,  0xcc, 0x14, 0xaa, 0x05, 0x0b, 0x77, 0x11, 0x6d, 0x8f, 0xcd, 0x80,
      0x4b,  0x45, 0x36, 0x6b, 0x5c, 0xae, 0x4a, 0x06, 0x82};
  ASSERT_EQ(1, stream.getPacketsCount());
  ASSERT_EQ(encoded.size(), sizeof encoded_expected);
  ASSERT_TRUE(0 == memcmp(encoded.data(), encoded_expected, encoded.size()));
}

TEST(OPackStream, WithoutInitializationPacketsCountIsZero) {
  RegionAllocator allocator(kPageSizeForAllocator, 1);
  cs::OPackStream stream(&allocator, kPublicKey);
  ASSERT_EQ(0, stream.getPacketsCount());
}

TEST(OPackStream, AfterClearPacketsCountIsZero) {
  RegionAllocator allocator(kPageSizeForAllocator, 1);
  cs::OPackStream stream(&allocator, kPublicKey);
  stream.init(BaseFlags::Fragmented);
  stream.clear();
  ASSERT_EQ(0, stream.getPacketsCount());
}

/*
TEST(OPackStream, WithoutInitializationEncodedDataIsEmpty) {
  RegionAllocator allocator(kPageSizeForAllocator, 1);
  cs::OPackStream stream(&allocator, kPublicKey);
  auto encoded = GetStreamData(stream);
  ASSERT_EQ(0, encoded.size());
}*/


TEST(OPackStream, getPacketsCount) {
  RegionAllocator allocator(kPageSizeForAllocator, 1);
  cs::OPackStream oPackStream(&allocator, kPublicKey);
  oPackStream.init(BaseFlags::Fragmented | BaseFlags::NetworkMsg);
  ASSERT_EQ(1, oPackStream.getPacketsCount());
}

TEST(OPackStream, getCurrentPtr) {
  RegionAllocator allocator(kPageSizeForAllocator, 1);
  cs::OPackStream oPackStream(&allocator, kPublicKey);
  oPackStream.init(BaseFlags::Fragmented | BaseFlags::NetworkMsg);
  ASSERT_EQ(1, (int)(*(oPackStream.getCurrentPtr() - 2)));
}

TEST(OPackStream, getCurrSize) {
  RegionAllocator allocator(kPageSizeForAllocator, 1);
  cs::OPackStream oPackStream(&allocator, kPublicKey);
  oPackStream.init(BaseFlags::Fragmented | BaseFlags::NetworkMsg);
  ASSERT_EQ(5, oPackStream.getCurrentSize());
}

template <class T, size_t ArraySize>
void TestConcreteTypeWriteToOPackStream(
    const T& value, const unsigned char (&expected_encoded_data)[ArraySize]) {
  RegionAllocator allocator(kPageSizeForAllocator, 1);
  cs::OPackStream stream(&allocator, kPublicKey);
  stream.init(BaseFlags::Fragmented | BaseFlags::NetworkMsg);
  stream << value;
  auto encoded = GetStreamData(stream);
  displayStreamData(stream);
  ASSERT_EQ(1, stream.getPacketsCount());
  ASSERT_EQ(encoded.size(), sizeof expected_encoded_data);
  ASSERT_TRUE(0 ==
              memcmp(encoded.data(), expected_encoded_data, encoded.size()));
}

TEST(OPackStream, IpAddressWrite) {
  const unsigned char expected[] = {0x03, 0x00, 0x00, 0x01, 0x00,
                                    0x7f, 0x00, 0x00, 0x01};
  TestConcreteTypeWriteToOPackStream(
      boost::asio::ip::address_v4::from_string("127.0.0.1"), expected);
}

TEST(OPackStream, StdStringWrite) {
  const unsigned char expected[] = {0x03, 0x00, 0x00, 0x01, 0x00, 0x0f, 0x00,
                                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x61,
                                    0x73, 0x63, 0x69, 0x69, 0x20, 0x73, 0x74,
                                    0x72, 0x69, 0x6e, 0x67, 0x20, 0x20, 0x20};
  TestConcreteTypeWriteToOPackStream(std::string("ascii string   "), expected);
}

TEST(OPackStream, BytesWrite) {
  const unsigned char expected[] = {
      0x03, 0x00, 0x00, 0x01, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x00};
  TestConcreteTypeWriteToOPackStream(cs::Bytes{1, 2, 3, 4, 5, 6, 7, 8, 9, 0},
                                     expected);
}

/*
csdb::Transaction CreateFakeTransaction() {
  const char kPublicKey[csdb::internal::kPublicKeySize] = {0};
  // const char kPrivateKey[csdb::internal::kPrivateKeySize] = {0};
  const int64_t id = 0x1234567890abcdef;
  const csdb::Address source_address =
      csdb::Address::from_public_key(kPublicKey);
  const csdb::Address target_address =
      csdb::Address::from_public_key(kPublicKey);
  auto currency = csdb::Currency{1};
  auto amount = csdb::Amount{10, 50};
  auto max_fee = csdb::AmountCommission{0.01};
  auto counted_fee = csdb::AmountCommission{0.01};
  std::string signature("12345678");

  return csdb::Transaction{id,     source_address, target_address, currency,
                           amount, max_fee,        counted_fee,    signature};
}

TEST(OPackStream, TransactionWrite) {
  const auto transaction = CreateFakeTransaction();
  const unsigned char expected[] = {0x00};
  TestConcreteTypeWriteToOPackStream(transaction, expected);
}  //*/

TEST(OPackStream, EmptyPoolWrite) {
  const unsigned char expected[] = {
      0x03, 0x00, 0x00, 0x01, 0x00, 0x2d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  TestConcreteTypeWriteToOPackStream(csdb::Pool{}, expected);
}

TEST(OPackStream, EmptyTransactionsPacketHashWrite) {
  const unsigned char expected[] = {0x03, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
                                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  TestConcreteTypeWriteToOPackStream(cs::TransactionsPacketHash{}, expected);
}

//
// TODO: TransactionsPacket, TransactionsPacketHash, Pool
//

TEST(OPackStream, EmptyTransactionsPacketWrite) {
  const unsigned char expected[] = {0x03, 0x00, 0x00, 0x01, 0x00, 0x08, 0x00,
                                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  TestConcreteTypeWriteToOPackStream(cs::TransactionsPacket{}, expected);
}

TEST(OPackStream, HashVectorWrite) {
  const unsigned char expected[] = {
      0x03, 0x00, 0x00, 0x01, 0x00, 0xee, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
      0x77, 0x88, 0x99, 0xaa, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
      0x99, 0xaa, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa,
      0x11, 0x22, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa,
      0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0x11, 0x22,
      0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0x11, 0x22, 0x11, 0x22,
      0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0x11, 0x22, 0x33, 0x44,
      0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
      0x77, 0x88, 0x99, 0xaa, 0x11, 0x22};
  cs::HashVector hash_vector{
      0xEE,
      cs::Hash{0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0x11,
               0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0x11, 0x22,
               0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0x11, 0x22},
      cs::Signature{0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA,
                    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA,
                    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA,
                    0x11, 0x22, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
                    0x99, 0xAA, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
                    0x99, 0xAA, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
                    0x99, 0xAA, 0x11, 0x22}};
  TestConcreteTypeWriteToOPackStream(hash_vector, expected);
}

TEST(OPackStream, HashMatrixrWrite) {
  const unsigned char expected[] = {
      0x03, 0x00, 0x00, 0x01, 0x00, 0xee, 0xee, 0x11, 0x22, 0x33, 0x44, 0x55,
      0x66, 0x77, 0x88, 0x99, 0xaa, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
      0x88, 0x99, 0xaa, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99,
      0xaa, 0x11, 0x22, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99,
      0xaa, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0x11,
      0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0x11, 0x22, 0x11,
      0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0x11, 0x22, 0x33,
      0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0x11, 0x22, 0x33, 0x44, 0x55,
      0x66, 0x77, 0x88, 0x99, 0xaa, 0x11, 0x22, 0xee, 0x11, 0x22, 0x33, 0x44,
      0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
      0x77, 0x88, 0x99, 0xaa, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
      0x99, 0xaa, 0x11, 0x22, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
      0x99, 0xaa, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa,
      0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0x11, 0x22,
      0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0x11, 0x22,
      0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0x11, 0x22, 0x33, 0x44,
      0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0x11, 0x22, 0xee, 0x11, 0x22, 0x33,
      0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0x11, 0x22, 0x33, 0x44, 0x55,
      0x66, 0x77, 0x88, 0x99, 0xaa, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
      0x88, 0x99, 0xaa, 0x11, 0x22, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
      0x88, 0x99, 0xaa, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99,
      0xaa, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0x11,
      0x22, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0x11,
      0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0x11, 0x22, 0x33,
      0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0x11, 0x22, 0xee, 0x11, 0x22,
      0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0x11, 0x22, 0x33, 0x44,
      0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
      0x77, 0x88, 0x99, 0xaa, 0x11, 0x22, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
      0x77, 0x88, 0x99, 0xaa, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
      0x99, 0xaa, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa,
      0x11, 0x22, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa,
      0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0x11, 0x22,
      0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0x11, 0x22, 0xee, 0x11,
      0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0x11, 0x22, 0x33,
      0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0x11, 0x22, 0x33, 0x44, 0x55,
      0x66, 0x77, 0x88, 0x99, 0xaa, 0x11, 0x22, 0x11, 0x22, 0x33, 0x44, 0x55,
      0x66, 0x77, 0x88, 0x99, 0xaa, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
      0x88, 0x99, 0xaa, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99,
      0xaa, 0x11, 0x22, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99,
      0xaa, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0x11,
      0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0x11, 0x22, 0x11,
      0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0x11, 0x22, 0x33,
      0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0x11, 0x22, 0x33, 0x44, 0x55,
      0x66, 0x77, 0x88, 0x99, 0xaa, 0x11, 0x22, 0x11, 0x22, 0x33, 0x44, 0x55,
      0x66, 0x77, 0x88, 0x99, 0xaa, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
      0x88, 0x99, 0xaa, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99,
      0xaa, 0x11, 0x22};
  cs::HashMatrix hash_matrix{
      0xEE,
      {{0xEE, cs::Hash{0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
                       0x99, 0xAA, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
                       0x77, 0x88, 0x99, 0xAA, 0x11, 0x22, 0x33, 0x44,
                       0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0x11, 0x22},
        cs::Signature{
            0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0x11,
            0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0x11, 0x22,
            0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0x11, 0x22, 0x11,
            0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0x11, 0x22,
            0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0x11, 0x22, 0x33,
            0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0x11, 0x22}},
       {0xEE, cs::Hash{0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
                       0x99, 0xAA, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
                       0x77, 0x88, 0x99, 0xAA, 0x11, 0x22, 0x33, 0x44,
                       0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0x11, 0x22},
        cs::Signature{
            0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0x11,
            0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0x11, 0x22,
            0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0x11, 0x22, 0x11,
            0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0x11, 0x22,
            0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0x11, 0x22, 0x33,
            0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0x11, 0x22}},
       {0xEE, cs::Hash{0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
                       0x99, 0xAA, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
                       0x77, 0x88, 0x99, 0xAA, 0x11, 0x22, 0x33, 0x44,
                       0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0x11, 0x22},
        cs::Signature{
            0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0x11,
            0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0x11, 0x22,
            0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0x11, 0x22, 0x11,
            0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0x11, 0x22,
            0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0x11, 0x22, 0x33,
            0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0x11, 0x22}},
       {0xEE, cs::Hash{0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
                       0x99, 0xAA, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
                       0x77, 0x88, 0x99, 0xAA, 0x11, 0x22, 0x33, 0x44,
                       0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0x11, 0x22},
        cs::Signature{
            0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0x11,
            0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0x11, 0x22,
            0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0x11, 0x22, 0x11,
            0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0x11, 0x22,
            0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0x11, 0x22, 0x33,
            0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0x11, 0x22}},
       {0xEE, cs::Hash{0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
                       0x99, 0xAA, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
                       0x77, 0x88, 0x99, 0xAA, 0x11, 0x22, 0x33, 0x44,
                       0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0x11, 0x22},
        cs::Signature{
            0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0x11,
            0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0x11, 0x22,
            0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0x11, 0x22, 0x11,
            0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0x11, 0x22,
            0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0x11, 0x22, 0x33,
            0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0x11, 0x22}}},
      cs::Signature{0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA,
                    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA,
                    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA,
                    0x11, 0x22, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
                    0x99, 0xAA, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
                    0x99, 0xAA, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
                    0x99, 0xAA, 0x11, 0x22}};
  TestConcreteTypeWriteToOPackStream(hash_matrix, expected);
}

TEST(OPackStream, EmptyPoolHashWrite) {
  const unsigned char expected[] = {0x03, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
                                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  TestConcreteTypeWriteToOPackStream(csdb::PoolHash{}, expected);
}

TEST(OPackStream, GeneralVectorWrite) {
  const unsigned char expected[] = {
      0x03, 0x00, 0x00, 0x01, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x78, 0x56, 0x34, 0x12, 0x21, 0x43, 0x65,
      0x87, 0xab, 0xab, 0xab, 0xab, 0xee, 0xee, 0xee, 0xee};
  std::vector<uint32_t> vector = {0x12345678, 0x87654321, 0xABABABAB,
                                  0xEEEEEEEE};
  TestConcreteTypeWriteToOPackStream(vector, expected);
}

TEST(OPackStream, ByteArrayWrite) {
  const unsigned char expected[] = {0x03, 0x00, 0x00, 0x01, 0x00,
                                    0x12, 0x34, 0x56, 0x78, 0x90,
                                    0xab, 0xcd, 0xef, 0x01, 0x23};
  cs::ByteArray<10> byte_array = {0x12, 0x34, 0x56, 0x78, 0x90,
                                  0xAB, 0xCD, 0xEF, 0x01, 0x23};
  TestConcreteTypeWriteToOPackStream(byte_array, expected);
}

TEST(OPackStream, GeneralIntegerWrite) {
  const unsigned char expected[] = {0x03, 0x00, 0x00, 0x01, 0x00,
                                    0x44, 0x03, 0x62, 0x67};
  uint32_t integer = 0x67620344;
  TestConcreteTypeWriteToOPackStream(integer, expected);
}
