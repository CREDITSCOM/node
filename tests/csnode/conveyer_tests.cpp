

#include <csdb/amount_commission.h>
#include <csdb/currency.h>
#include <gtest/gtest.h>
#include <csnode/conveyer.hpp>
#include <iostream>

const cs::RoundNumber kRoundNumber = 12345;
const cs::PublicKey kPublicKey = {
    0x53, 0x4B, 0xD3, 0xDF, 0x77, 0x29, 0xFD, 0xCF, 0xEA, 0x4A, 0xCD,
    0x0E, 0xCC, 0x14, 0xAA, 0x05, 0x0B, 0x77, 0x11, 0x6D, 0x8F, 0xCD,
    0x80, 0x4B, 0x45, 0x36, 0x6B, 0x5C, 0xAE, 0x4A, 0x06, 0x82};

const size_t kNumberOfConfidants = 7;
const cs::ConfidantsKeys kConfidantsKeys = {
    {0X53, 0X4B, 0XD3, 0XDF, 0X77, 0X29, 0XFD, 0XCF, 0XEA, 0X4A, 0XCD,
     0X0E, 0XCC, 0X14, 0XAA, 0X05, 0X0B, 0X77, 0X11, 0X6D, 0X8F, 0XCD,
     0X80, 0X4B, 0X45, 0X36, 0X6B, 0X5C, 0XAE, 0X4A, 0X06, 0X82},
    {0X53, 0X4B, 0XD3, 0XDF, 0X77, 0X29, 0XFD, 0XCF, 0XEA, 0X4A, 0XCD,
     0X0E, 0XCC, 0X14, 0XAA, 0X05, 0X0B, 0X77, 0X11, 0X6D, 0X8F, 0XCD,
     0X80, 0X4B, 0X45, 0X36, 0X6B, 0X5C, 0XAE, 0X4A, 0X06, 0X82},
    {0X53, 0X4B, 0XD3, 0XDF, 0X77, 0X29, 0XFD, 0XCF, 0XEA, 0X4A, 0XCD,
     0X0E, 0XCC, 0X14, 0XAA, 0X05, 0X0B, 0X77, 0X11, 0X6D, 0X8F, 0XCD,
     0X80, 0X4B, 0X45, 0X36, 0X6B, 0X5C, 0XAE, 0X4A, 0X06, 0X82},
    {0X53, 0X4B, 0XD3, 0XDF, 0X77, 0X29, 0XFD, 0XCF, 0XEA, 0X4A, 0XCD,
     0X0E, 0XCC, 0X14, 0XAA, 0X05, 0X0B, 0X77, 0X11, 0X6D, 0X8F, 0XCD,
     0X80, 0X4B, 0X45, 0X36, 0X6B, 0X5C, 0XAE, 0X4A, 0X06, 0X82},
    {0X53, 0X4B, 0XD3, 0XDF, 0X77, 0X29, 0XFD, 0XCF, 0XEA, 0X4A, 0XCD,
     0X0E, 0XCC, 0X14, 0XAA, 0X05, 0X0B, 0X77, 0X11, 0X6D, 0X8F, 0XCD,
     0X80, 0X4B, 0X45, 0X36, 0X6B, 0X5C, 0XAE, 0X4A, 0X06, 0X82},
    {0X53, 0X4B, 0XD3, 0XDF, 0X77, 0X29, 0XFD, 0XCF, 0XEA, 0X4A, 0XCD,
     0X0E, 0XCC, 0X14, 0XAA, 0X05, 0X0B, 0X77, 0X11, 0X6D, 0X8F, 0XCD,
     0X80, 0X4B, 0X45, 0X36, 0X6B, 0X5C, 0XAE, 0X4A, 0X06, 0X82},
    {0X53, 0X4B, 0XD3, 0XDF, 0X77, 0X29, 0XFD, 0XCF, 0XEA, 0X4A, 0XCD,
     0X0E, 0XCC, 0X14, 0XAA, 0X05, 0X0B, 0X77, 0X11, 0X6D, 0X8F, 0XCD,
     0X80, 0X4B, 0X45, 0X36, 0X6B, 0X5C, 0XAE, 0X4A, 0X06, 0X82}};

const cs::Characteristic kCharacteristic = {{0xEE, 0xEE, 0xEd}};

namespace cs {
bool operator==(const cs::RoundTable& left, const cs::RoundTable& right) {
  auto round_is_equal = left.round == right.round,
       general_is_equal = left.general == right.general,
       confidants_is_equal = left.confidants == right.confidants,
       hashes_is_equal = left.hashes == right.hashes,
       charBytes_is_equal = left.charBytes.mask == right.charBytes.mask;

  return round_is_equal && general_is_equal && confidants_is_equal &&
         hashes_is_equal && charBytes_is_equal;
}

bool operator==(const cs::Characteristic& left,
                const cs::Characteristic& right) {
  return left.mask == right.mask;
}
}  // namespace cs

namespace csdb {
bool operator==(const csdb::Transaction& left, const csdb::Transaction& right) {
  return left.id() == right.id() && left.innerID() == right.innerID() &&
         left.source() == right.source() && left.target() == right.target() &&
         left.currency() == right.currency() &&
         left.max_fee() == right.max_fee() &&
         left.counted_fee() == right.counted_fee() &&
         left.signature() == right.signature();
}
}  // namespace csdb

TEST(Conveyer, CharacteristicSetAndGet) {
  auto& conveyer{cs::Conveyer::instance()};
  conveyer.setCharacteristic(kCharacteristic, kRoundNumber);
  ASSERT_EQ(kCharacteristic, *conveyer.characteristic(kRoundNumber));
}

csdb::Transaction CreateTestTransaction(const int64_t id,
                                        const uint8_t amount) {
  csdb::Transaction transaction{
      id,
      csdb::Address::from_public_key(
          {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
           0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
           0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01}),
      csdb::Address::from_public_key(
          {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
           0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
           0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02}),
      csdb::Currency{amount},
      csdb::Amount{0, 0},
      csdb::AmountCommission{0.},
      csdb::AmountCommission{0.},
      std::string(
          "0000000000000000000000000000000000000000000000000000000000000000")};
  return transaction;
}

auto CreateTestPacket(const size_t number_of_transactions) {
  cs::TransactionsPacket packet{};
  for (size_t i = 0; i < number_of_transactions; ++i) {
    packet.addTransaction(CreateTestTransaction(0x1234567800000001 + i, 1));
  }
  packet.makeHash();
  std::cout << "hash = " << packet.hash().toString() << std::endl;
  return packet;
}

auto CreateTestRoundTable(const cs::Hashes& hashes) {
  return cs::RoundTable{kRoundNumber, kPublicKey, kConfidantsKeys, hashes,
                        kCharacteristic};
}

TEST(Conveyer, RoundTableReturnsNullIfRoundDoesNotExist) {
  auto& conveyer{cs::Conveyer::instance()};
  ASSERT_EQ(conveyer.roundTable(1), nullptr);
}

TEST(Conveyer, MainLogic) {
  auto packet = CreateTestPacket(20);
  auto&& packet_copy{cs::TransactionsPacket{packet}};
  auto& conveyer{cs::Conveyer::instance()};
  auto hash = packet_copy.hash();
  conveyer.setRound(CreateTestRoundTable({packet_copy.hash()}));
  ASSERT_EQ(1, conveyer.currentNeededHashes().size());

  conveyer.addFoundPacket(kRoundNumber, std::move(packet_copy));
  ASSERT_TRUE(conveyer.currentNeededHashes().empty());
  ASSERT_TRUE(conveyer.isSyncCompleted());

  auto created_packet{conveyer.createPacket()};
  ASSERT_TRUE(created_packet.has_value());
  ASSERT_EQ(packet.transactionsCount(),
            created_packet.value().transactionsCount());

  created_packet.value().makeHash();
  ASSERT_EQ(packet.hash(), created_packet.value().hash());

  const auto characteristic{cs::Characteristic{
      {0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0}}};
  conveyer.setCharacteristic(characteristic, kRoundNumber);
  ASSERT_EQ(characteristic, *conveyer.characteristic(kRoundNumber));
  auto characteristic_hash{conveyer.characteristicHash(kRoundNumber)};
  const cs::Hash kCharacteristicHash{
      0xc8, 0xb0, 0xaa, 0x4c, 0x78, 0x28, 0xb8, 0xe2, 0x04, 0x69, 0x23,
      0x3a, 0x47, 0x7e, 0x12, 0x56, 0x63, 0x33, 0x82, 0x1b, 0x0c, 0xd4,
      0x9d, 0xd4, 0x99, 0xd1, 0x87, 0x9c, 0x41, 0xff, 0xf1, 0x1f};
  ASSERT_EQ(characteristic_hash, kCharacteristicHash);

  cs::PoolMetaInfo pool_meta_info{"1542617459297", kRoundNumber};
  auto pool{conveyer.applyCharacteristic(pool_meta_info)};
  ASSERT_TRUE(pool.has_value());
  ASSERT_EQ(3, pool.value().transactions_count());
  ASSERT_EQ(packet.transactions().at(2), pool.value().transaction(0));
  ASSERT_EQ(packet.transactions().at(9), pool.value().transaction(1));
  ASSERT_EQ(packet.transactions().at(16), pool.value().transaction(2));
}
