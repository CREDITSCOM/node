#include <gtest/gtest.h>

#include <csdb/amount_commission.hpp>
#include <csdb/currency.hpp>

#include <csnode/conveyer.hpp>

#include <iostream>

#include <lib/system/hash.hpp>

const cs::RoundNumber kRoundNumber = 12345;
[[maybe_unused]]
const cs::PublicKey kPublicKey = {0x53, 0x4B, 0xD3, 0xDF, 0x77, 0x29, 0xFD, 0xCF, 0xEA, 0x4A, 0xCD, 0x0E, 0xCC, 0x14, 0xAA, 0x05,
                                  0x0B, 0x77, 0x11, 0x6D, 0x8F, 0xCD, 0x80, 0x4B, 0x45, 0x36, 0x6B, 0x5C, 0xAE, 0x4A, 0x06, 0x82};

const cs::ConfidantsKeys kConfidantsKeys = {{0X53, 0X4B, 0XD3, 0XDF, 0X77, 0X29, 0XFD, 0XCF, 0XEA, 0X4A, 0XCD, 0X0E, 0XCC, 0X14, 0XAA, 0X05,
                                             0X0B, 0X77, 0X11, 0X6D, 0X8F, 0XCD, 0X80, 0X4B, 0X45, 0X36, 0X6B, 0X5C, 0XAE, 0X4A, 0X06, 0X82},
                                            {0X53, 0X4B, 0XD3, 0XDF, 0X77, 0X29, 0XFD, 0XCF, 0XEA, 0X4A, 0XCD, 0X0E, 0XCC, 0X14, 0XAA, 0X05,
                                             0X0B, 0X77, 0X11, 0X6D, 0X8F, 0XCD, 0X80, 0X4B, 0X45, 0X36, 0X6B, 0X5C, 0XAE, 0X4A, 0X06, 0X82},
                                            {0X53, 0X4B, 0XD3, 0XDF, 0X77, 0X29, 0XFD, 0XCF, 0XEA, 0X4A, 0XCD, 0X0E, 0XCC, 0X14, 0XAA, 0X05,
                                             0X0B, 0X77, 0X11, 0X6D, 0X8F, 0XCD, 0X80, 0X4B, 0X45, 0X36, 0X6B, 0X5C, 0XAE, 0X4A, 0X06, 0X82},
                                            {0X53, 0X4B, 0XD3, 0XDF, 0X77, 0X29, 0XFD, 0XCF, 0XEA, 0X4A, 0XCD, 0X0E, 0XCC, 0X14, 0XAA, 0X05,
                                             0X0B, 0X77, 0X11, 0X6D, 0X8F, 0XCD, 0X80, 0X4B, 0X45, 0X36, 0X6B, 0X5C, 0XAE, 0X4A, 0X06, 0X82},
                                            {0X53, 0X4B, 0XD3, 0XDF, 0X77, 0X29, 0XFD, 0XCF, 0XEA, 0X4A, 0XCD, 0X0E, 0XCC, 0X14, 0XAA, 0X05,
                                             0X0B, 0X77, 0X11, 0X6D, 0X8F, 0XCD, 0X80, 0X4B, 0X45, 0X36, 0X6B, 0X5C, 0XAE, 0X4A, 0X06, 0X82},
                                            {0X53, 0X4B, 0XD3, 0XDF, 0X77, 0X29, 0XFD, 0XCF, 0XEA, 0X4A, 0XCD, 0X0E, 0XCC, 0X14, 0XAA, 0X05,
                                             0X0B, 0X77, 0X11, 0X6D, 0X8F, 0XCD, 0X80, 0X4B, 0X45, 0X36, 0X6B, 0X5C, 0XAE, 0X4A, 0X06, 0X82},
                                            {0X53, 0X4B, 0XD3, 0XDF, 0X77, 0X29, 0XFD, 0XCF, 0XEA, 0X4A, 0XCD, 0X0E, 0XCC, 0X14, 0XAA, 0X05,
                                             0X0B, 0X77, 0X11, 0X6D, 0X8F, 0XCD, 0X80, 0X4B, 0X45, 0X36, 0X6B, 0X5C, 0XAE, 0X4A, 0X06, 0X82}};

const cs::Characteristic kCharacteristic = {{0xEE, 0xEE, 0xEd}};

namespace cs {
bool operator==(const cs::RoundTable& left, const cs::RoundTable& right) {
    auto round_is_equal = left.round == right.round, /*general_is_equal = left.general == right.general,*/ confidants_is_equal = left.confidants == right.confidants,
         hashes_is_equal = left.hashes == right.hashes/*, charBytes_is_equal = left.characteristic.mask == right.characteristic.mask*/;

    return round_is_equal && /*general_is_equal && */confidants_is_equal && hashes_is_equal/* && charBytes_is_equal*/;
}

bool operator==(const cs::Characteristic& left, const cs::Characteristic& right) {
    return left.mask == right.mask;
}
}  // namespace cs

namespace csdb {
bool operator==(const csdb::Transaction& left, const csdb::Transaction& right) {
    auto innerIdResult = left.innerID() == right.innerID();
    auto idResult = left.id() == right.id();
    auto signatureResult = left.signature() == right.signature();
    auto sourceResult = left.source() == right.source();
    auto currencyResult = left.currency() == right.currency();
    auto targetResult = left.target() == right.target();
    auto feeResult = left.max_fee().to_double() == right.max_fee().to_double();
    auto countedFeeResult = left.counted_fee().to_double() == right.counted_fee().to_double();

    return innerIdResult && idResult && signatureResult && sourceResult && currencyResult && targetResult && feeResult & countedFeeResult;
}
}  // namespace csdb

csdb::Transaction CreateTestTransaction(const int64_t id, const uint8_t amount) {
    cs::Signature sign;
    sign.fill(0);

    csdb::Transaction transaction{id,
                                  csdb::Address::from_public_key(cs::PublicKey{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                                                               0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01}),
                                  csdb::Address::from_public_key(cs::PublicKey{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                                                               0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02}),
                                  csdb::Currency{amount},
                                  csdb::Amount{0, 0},
                                  csdb::AmountCommission{0.},
                                  csdb::AmountCommission{0.},
                                  sign};

    return transaction;
}

auto CreateTestPacket(const size_t number_of_transactions) {
    cs::TransactionsPacket packet;

    for (size_t i = 0; i < number_of_transactions; ++i) {
        size_t value = 0x1234567800000001;
        packet.addTransaction(CreateTestTransaction(static_cast<int64_t>(value + i), static_cast<uint8_t>(1)));
    }

    packet.makeHash();
    cslog() << "hash = " << packet.hash().toString();
    return packet;
}

auto CreateTestRoundTable(const cs::PacketsHashes& hashes) {
    return cs::RoundTable{kRoundNumber, /*kPublicKey, */kConfidantsKeys, hashes/*, kCharacteristic*/};
}

TEST(TransactionsEqualityOperator, SameAreEqual) {
    auto transaction1 = CreateTestTransaction(123, uint8_t{45}), transaction2 = transaction1;
    ASSERT_EQ(transaction1, transaction2);
    ASSERT_TRUE(transaction1 == transaction2);
}

TEST(TransactionsEqualityOperator, DifferentId) {
    ASSERT_FALSE(CreateTestTransaction(123, uint8_t{45}) == CreateTestTransaction(321, uint8_t{45}));
}

TEST(TransactionsEqualityOperator, DifferentAmount) {
    ASSERT_FALSE(CreateTestTransaction(123, uint8_t{45}) == CreateTestTransaction(123, uint8_t{00}));
}

class ConveyerTest : public cs::ConveyerBase {
public:
    ConveyerTest()
    : ConveyerBase() {
    }
    ~ConveyerTest() = default;
};

TEST(Conveyer, RoundTableReturnsNullIfRoundDoesNotExist) {
    ConveyerTest conveyer{};
    ASSERT_EQ(conveyer.roundTable(1), nullptr);
}

TEST(Conveyer, GetCharacteristicReturnsNullIfConveyerHasNoMeta) {
    ConveyerTest conveyer{};
    const auto kAnyRoundNumber{123};
    ASSERT_EQ(nullptr, conveyer.characteristic(kAnyRoundNumber));
}

TEST(Conveyer, RoundTableReturnsSameAsThatWasSetWithSetRound) {
    ConveyerTest conveyer{};
    auto round_table{CreateTestRoundTable({CreateTestPacket(2).hash()})};
    auto&& round_table_copy{cs::RoundTable{round_table}};
    conveyer.setRound(round_table.round);
    conveyer.setTable(round_table_copy);
    ASSERT_EQ(round_table, conveyer.currentRoundTable());
}

TEST(Conveyer, SetRoundDoesNotSetInvalidRoundNumber) {
    ConveyerTest conveyer{};
    auto&& round_table{CreateTestRoundTable({CreateTestPacket(2).hash()})};
    auto&& incorrect_round_table{cs::RoundTable{round_table}};
    conveyer.setTable(std::move(round_table));
    ASSERT_EQ(round_table.round, conveyer.currentRoundNumber());
    incorrect_round_table.round = round_table.round - 1;
    conveyer.setTable(std::move(incorrect_round_table));
    ASSERT_EQ(round_table.round, conveyer.currentRoundNumber());
    incorrect_round_table.round = 0;
    conveyer.setTable(std::move(incorrect_round_table));
    ASSERT_EQ(round_table.round, conveyer.currentRoundNumber());
}

TEST(Conveyer, RoundTableReturnsNullIfRoundWasNotAdded) {
    ConveyerTest conveyer{};
    auto&& round_table{CreateTestRoundTable({CreateTestPacket(2).hash()})};
    conveyer.setTable(std::move(round_table));
    ASSERT_EQ(conveyer.roundTable(1), nullptr);
}

TEST(Conveyer, AddTransaction) {
    ConveyerTest conveyer{};
    auto transaction{CreateTestTransaction(3, 1)};
    conveyer.addTransaction(transaction);
    auto& transactions_block = conveyer.packetQueue();
    ASSERT_EQ(1, conveyer.packetQueue().size());
    auto packet{cs::TransactionsPacket{}};
    packet.addTransaction(transaction);
    ASSERT_EQ(packet.toBinary(), transactions_block.back().toBinary());
}

TEST(Conveyer, TransactionPacketTableIsEmptyAtCreation) {
    constexpr auto size = 0;
    ConveyerTest conveyer{};
    auto& table = conveyer.transactionsPacketTable();
    ASSERT_EQ(table.size(), size);
}

TEST(Conveyer, CanSuccessfullyAddTransactionsPacket) {
    ConveyerTest conveyer{};
    auto packet = CreateTestPacket(2);
    conveyer.addTransactionsPacket(packet);
    auto& table{conveyer.transactionsPacketTable()};
    ASSERT_EQ(table.at(packet.hash()).toBinary(cs::TransactionsPacket::Serialization::Transactions), packet.toBinary(cs::TransactionsPacket::Serialization::Transactions));
}

TEST(Conveyer, CanAddTransactionToLastBlock) {
    ConveyerTest conveyer{};
    auto& table = conveyer.packetQueue();
    ASSERT_TRUE(table.isEmpty());
    auto transaction1 = CreateTestTransaction(1, 1), transaction2 = CreateTestTransaction(2, 1);
    conveyer.addTransaction(transaction1);
    conveyer.addTransaction(transaction2);
    ASSERT_EQ(1, table.size());
    ASSERT_EQ(2, table.back().transactionsCount());
    ASSERT_EQ(transaction1, table.back().transactions().at(0));
    ASSERT_EQ(transaction2, table.back().transactions().at(1));
}

TEST(Conveyer, MainLogic) {
    auto packet = CreateTestPacket(20);
    auto&& packet_copy{cs::TransactionsPacket{packet}};
    ConveyerTest conveyer{};

    auto hash = packet_copy.hash();
    auto table = CreateTestRoundTable({packet_copy.hash()});

    conveyer.setTable(table);
    ASSERT_EQ(1, conveyer.currentNeededHashes().size());

    conveyer.addFoundPacket(kRoundNumber, std::move(packet_copy));
    ASSERT_TRUE(conveyer.currentNeededHashes().empty());
    ASSERT_TRUE(conveyer.isSyncCompleted());

    auto created_packet{conveyer.createPacket(1)};
    ASSERT_TRUE(created_packet.has_value());
    ASSERT_EQ(packet.transactionsCount(), created_packet.value().first.transactionsCount());

    created_packet.value().first.makeHash();
    ASSERT_EQ(packet.hash(), created_packet.value().first.hash());

    const auto characteristic{cs::Characteristic{{0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0}}};
    conveyer.setCharacteristic(characteristic, kRoundNumber);

    cs::Hash expectedHash = generateHash(characteristic.mask.data(), characteristic.mask.size());

    ASSERT_EQ(characteristic, *conveyer.characteristic(kRoundNumber));

    auto characteristic_hash = conveyer.characteristicHash(kRoundNumber);

    ASSERT_EQ(characteristic_hash, expectedHash);

    cs::PublicKey pk;
    pk.fill(0);

    csdb::PoolHash ph;
    cs::Bytes tmpCharacteristic;
    cs::PoolMetaInfo pool_meta_info{ { tmpCharacteristic }, "1542617459297", ph, kRoundNumber, cs::Bytes{}, std::vector<csdb::Pool::SmartSignature>{}};

    auto pool{conveyer.applyCharacteristic(pool_meta_info)};

    ASSERT_TRUE(pool.has_value());
    ASSERT_EQ(3, pool.value().transactions_count());
    ASSERT_EQ(packet.transactions().at(2), pool.value().transaction(0));
    ASSERT_EQ(packet.transactions().at(9), pool.value().transaction(1));
    ASSERT_EQ(packet.transactions().at(16), pool.value().transaction(2));
}

TEST(Conveyer, TestSendCache) {
    cs::PacketsHashes hashes;

    size_t counter = 0;
    ConveyerTest conveyer{};

    conveyer.setRound(0);

    cs::Connector::connect(&conveyer.packetFlushed, [&](const auto& packet) {
        if (counter < 2) {
            hashes.push_back(packet.hash());
        }

        ++counter;
    });

    for (size_t i = 0; i < 20; ++i) {
        size_t value = 0x1234567800000001;
        conveyer.addTransaction(CreateTestTransaction(static_cast<int64_t>(value + i), static_cast<uint8_t>(1)));

        if ((i == 9) || (i == 19)) {
            conveyer.flushTransactions();
        }
    }

    ASSERT_EQ(conveyer.packetQueueTransactionsCount(), 0);
    ASSERT_EQ(conveyer.sendCacheCount(), 2);
    ASSERT_EQ(counter, 2);

    // try to resend
    const cs::RoundNumber testRound = 100;

    conveyer.setRound(testRound);

    // add new ones
    size_t value = 0x1234567800000001;
    conveyer.addTransaction(CreateTestTransaction(static_cast<int64_t>(value + 30), static_cast<uint8_t>(1)));

    conveyer.flushTransactions();

    ASSERT_EQ(counter, 5);
    ASSERT_EQ(conveyer.sendCacheCount(), 3);

    conveyer.flushTransactions();

    ASSERT_EQ(counter, 5);

    cs::RoundTable table;
    table.round = testRound;
    table.hashes = hashes;

    conveyer.setTable(std::move(table));

    const auto characteristic{cs::Characteristic{{0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0}}};
    conveyer.setCharacteristic(characteristic, testRound);

    csdb::PoolHash ph;
    cs::Bytes tmpCharacteristic;
    cs::PoolMetaInfo metaInfo{ { tmpCharacteristic }, "1542617459297", ph, testRound, cs::Bytes{}, std::vector<csdb::Pool::SmartSignature>{}};

    auto pool { conveyer.applyCharacteristic(metaInfo) };

    ASSERT_EQ(conveyer.sendCacheCount(), 1);

    conveyer.flushTransactions();

    ASSERT_EQ(counter, 5);
    ASSERT_EQ(conveyer.sendCacheCount(), 1);
    ASSERT_EQ(pool->transactions().size(), 3);
}

TEST(Conveyer, TestRejectedHashes) {
    bool called = false;

    ConveyerTest conveyer{};
    conveyer.setRound(0);

    auto packet1 = CreateTestPacket(20);
    auto packet2 = CreateTestPacket(25);

    conveyer.addTransactionsPacket(packet1);
    conveyer.addTransactionsPacket(packet2);

    conveyer.flushTransactions();

    cs::Connector::connect(&conveyer.packetFlushed, [&](const auto&) {
        called = true;
    });

    ASSERT_TRUE(conveyer.addRejectedHashToCache(packet2.hash()));

    conveyer.flushTransactions();

    ASSERT_FALSE(called);
    ASSERT_FALSE(conveyer.addRejectedHashToCache(packet2.hash()));

    conveyer.setRound(100);
    conveyer.flushTransactions();

    ASSERT_TRUE(called);
}

TEST(Conveyer, TestRoundChangeSignal) {
    bool called = false;

    ConveyerTest conveyer{};
    conveyer.setRound(0);

    cs::Connector::connect(&conveyer.roundChanged, [&](const cs::RoundNumber) {
        called = true;
    });

    conveyer.setRound(0);
    ASSERT_FALSE(called);

    conveyer.setRound(100);
    ASSERT_TRUE(called);

    called = false;

    cs::RoundTable table;
    table.round = 100;

    conveyer.setTable(table);
    ASSERT_FALSE(called);

    table.round = 100500;

    conveyer.setTable(table);
    ASSERT_TRUE(called);
}
