#include "csdb/wallet.h"

#include "csdb_unit_tests_environment.h"

#include "csdb/internal/utils.h"

class WalletTest : public ::testing::Test
{
protected:
  WalletTest() :
    path_to_tests_(::csdb::internal::app_data_path() + "csdb_unittests_wallet")
  {
  }

  void TearDown() override
  {
    ASSERT_TRUE(::csdb::internal::path_remove(path_to_tests_));
  }

  ::std::string path_to_tests_;
  ::csdb::Address addr1 = ::csdb::Address::from_string("0000000000000000000000000000000000000000");
  ::csdb::Address addr2 = ::csdb::Address::from_string("0000000000000000000000000000000000000001");
  ::csdb::Address addr3 = ::csdb::Address::from_string("0000000000000000000000000000000000000002");
};

using namespace ::csdb;

TEST_F(WalletTest, Empty)
{
  Wallet w;
  EXPECT_FALSE(w.is_valid());
  EXPECT_FALSE(w.address().is_valid());
  EXPECT_TRUE(w.currencies().empty());
  EXPECT_EQ(w.amount(Currency("RUB")), 0_c);
}

TEST_F(WalletTest, ClosedStorage)
{
  Storage s;
  Wallet w = Wallet::get(addr1, s);
  EXPECT_FALSE(w.is_valid());
  EXPECT_FALSE(w.address().is_valid());
  EXPECT_TRUE(w.currencies().empty());
  EXPECT_EQ(w.amount(Currency("RUB")), 0_c);
}

TEST_F(WalletTest, EmptyStorage)
{
  Storage s;
  ASSERT_TRUE(s.open(path_to_tests_));

  Wallet w = Wallet::get(addr1, s);
  EXPECT_TRUE(w.is_valid());
  EXPECT_TRUE(w.address().is_valid());
  EXPECT_TRUE(w.currencies().empty());
  EXPECT_EQ(w.amount(Currency("RUB")), 0_c);
}

TEST_F(WalletTest, OnePool)
{
  Storage s;
  ASSERT_TRUE(s.open(path_to_tests_));
  ASSERT_TRUE(s.last_hash().is_empty());

  Pool p1{s.last_hash(), 0, s};
  ASSERT_TRUE(p1.add_transaction(Transaction(addr1, addr2, Currency("RUB"), 10_c), true));
  ASSERT_TRUE(p1.add_transaction(Transaction(addr2, addr3, Currency("RUB"), 20_c), true));
  ASSERT_TRUE(p1.add_transaction(Transaction(addr3, addr1, Currency("RUB"), 30_c), true));
  ASSERT_TRUE(p1.compose());
  ASSERT_TRUE(p1.save());

  Wallet w1 = Wallet::get(addr1, s);
  EXPECT_TRUE(w1.is_valid());
  EXPECT_TRUE(w1.address().is_valid());
  EXPECT_EQ(w1.currencies().size(), static_cast<size_t>(1));
  EXPECT_EQ(w1.amount(Currency("RUB")), 20_c);

  Wallet w2 = Wallet::get(addr2, s);
  EXPECT_TRUE(w2.is_valid());
  EXPECT_TRUE(w2.address().is_valid());
  EXPECT_EQ(w2.currencies().size(), static_cast<size_t>(1));
  EXPECT_EQ(w2.amount(Currency("RUB")), -10_c);

  Wallet w3 = Wallet::get(addr3, s);
  EXPECT_TRUE(w3.is_valid());
  EXPECT_TRUE(w3.address().is_valid());
  EXPECT_EQ(w3.currencies().size(), static_cast<size_t>(1));
  EXPECT_EQ(w3.amount(Currency("RUB")), -10_c);
}

TEST_F(WalletTest, MultiPoolOneCurrency)
{
  Storage s;
  ASSERT_TRUE(s.open(path_to_tests_));
  ASSERT_TRUE(s.last_hash().is_empty());

  Pool p{s.last_hash(), 0, s};
  ASSERT_TRUE(p.add_transaction(Transaction(addr1, addr2, Currency("RUB"), 10_c), true));
  ASSERT_TRUE(p.add_transaction(Transaction(addr2, addr3, Currency("RUB"), 20_c), true));
  ASSERT_TRUE(p.add_transaction(Transaction(addr3, addr1, Currency("RUB"), 30_c), true));
  ASSERT_TRUE(p.compose());
  ASSERT_TRUE(p.save());
  ASSERT_EQ(s.last_hash(), p.hash());

  p = Pool{p.hash(), p.sequence() + 1, p.storage()};
  ASSERT_TRUE(p.compose());
  ASSERT_TRUE(p.save());
  ASSERT_EQ(s.last_hash(), p.hash());

  p = Pool{p.hash(), p.sequence() + 1, p.storage()};
  ASSERT_TRUE(p.add_transaction(Transaction(addr1, addr2, Currency("RUB"), 300_c), true));
  ASSERT_TRUE(p.add_transaction(Transaction(addr2, addr3, Currency("RUB"), 200_c), true));
  ASSERT_TRUE(p.add_transaction(Transaction(addr3, addr1, Currency("RUB"), 150_c), true));
  ASSERT_TRUE(p.compose());
  ASSERT_TRUE(p.save());
  ASSERT_EQ(s.last_hash(), p.hash());

  Wallet w1 = Wallet::get(addr1, s);
  EXPECT_TRUE(w1.is_valid());
  EXPECT_TRUE(w1.address().is_valid());
  EXPECT_EQ(w1.currencies().size(), static_cast<size_t>(1));
  EXPECT_EQ(w1.amount(Currency("RUB")), -130_c);

  Wallet w2 = Wallet::get(addr2, s);
  EXPECT_TRUE(w2.is_valid());
  EXPECT_TRUE(w2.address().is_valid());
  EXPECT_EQ(w2.currencies().size(), static_cast<size_t>(1));
  EXPECT_EQ(w2.amount(Currency("RUB")), 90_c);

  Wallet w3 = Wallet::get(addr3, s);
  EXPECT_TRUE(w3.is_valid());
  EXPECT_TRUE(w3.address().is_valid());
  EXPECT_EQ(w3.currencies().size(), static_cast<size_t>(1));
  EXPECT_EQ(w3.amount(Currency("RUB")), 40_c);
}

TEST_F(WalletTest, MultiPoolMultiCurrency)
{
  Storage s;
  ASSERT_TRUE(s.open(path_to_tests_));
  ASSERT_TRUE(s.last_hash().is_empty());

  Pool p{s.last_hash(), 0, s};
  ASSERT_TRUE(p.add_transaction(Transaction(addr1, addr2, Currency("RUB"), 10_c), true));
  ASSERT_TRUE(p.add_transaction(Transaction(addr2, addr3, Currency("RUB"), 20_c), true));
  ASSERT_TRUE(p.add_transaction(Transaction(addr3, addr1, Currency("RUB"), 30_c), true));
  ASSERT_TRUE(p.compose());
  ASSERT_TRUE(p.save());
  ASSERT_EQ(s.last_hash(), p.hash());

  p = Pool{p.hash(), p.sequence() + 1, p.storage()};
  ASSERT_TRUE(p.compose());
  ASSERT_TRUE(p.save());
  ASSERT_EQ(s.last_hash(), p.hash());

  p = Pool{p.hash(), p.sequence() + 1, p.storage()};
  ASSERT_TRUE(p.add_transaction(Transaction(addr1, addr2, Currency("RUB"), 300_c), true));
  ASSERT_TRUE(p.add_transaction(Transaction(addr2, addr3, Currency("RUB"), 200_c), true));
  ASSERT_TRUE(p.add_transaction(Transaction(addr3, addr1, Currency("RUB"), 150_c), true));
  ASSERT_TRUE(p.compose());
  ASSERT_TRUE(p.save());
  ASSERT_EQ(s.last_hash(), p.hash());

  p = Pool{p.hash(), p.sequence() + 1, p.storage()};
  ASSERT_TRUE(p.compose());
  ASSERT_TRUE(p.save());
  ASSERT_EQ(s.last_hash(), p.hash());

  p = Pool{p.hash(), p.sequence() + 1, p.storage()};
  ASSERT_TRUE(p.add_transaction(Transaction(addr1, addr2, Currency("USD"), 0.05_c), true));
  ASSERT_TRUE(p.add_transaction(Transaction(addr2, addr3, Currency("USD"), 0.06_c), true));
  ASSERT_TRUE(p.add_transaction(Transaction(addr3, addr1, Currency("USD"), 0.07_c), true));
  ASSERT_TRUE(p.compose());
  ASSERT_TRUE(p.save());
  ASSERT_EQ(s.last_hash(), p.hash());

  p = Pool{p.hash(), p.sequence() + 1, p.storage()};
  ASSERT_TRUE(p.compose());
  ASSERT_TRUE(p.save());
  ASSERT_EQ(s.last_hash(), p.hash());

  p = Pool{p.hash(), p.sequence() + 1, p.storage()};
  ASSERT_TRUE(p.add_transaction(Transaction(addr1, addr2, Currency("USD"), 0.19_c), true));
  ASSERT_TRUE(p.add_transaction(Transaction(addr2, addr3, Currency("USD"), 0.18_c), true));
  ASSERT_TRUE(p.add_transaction(Transaction(addr3, addr1, Currency("USD"), 0.09_c), true));
  ASSERT_TRUE(p.compose());
  ASSERT_TRUE(p.save());
  ASSERT_EQ(s.last_hash(), p.hash());

  Wallet w1 = Wallet::get(addr1, s);
  EXPECT_TRUE(w1.is_valid());
  EXPECT_TRUE(w1.address().is_valid());
  EXPECT_EQ(w1.currencies().size(), static_cast<size_t>(2));
  EXPECT_EQ(w1.amount(Currency("RUB")), -130_c);
  EXPECT_EQ(w1.amount(Currency("USD")), -0.08_c);

  Wallet w2 = Wallet::get(addr2, s);
  EXPECT_TRUE(w2.is_valid());
  EXPECT_TRUE(w2.address().is_valid());
  EXPECT_EQ(w2.currencies().size(), static_cast<size_t>(2));
  EXPECT_EQ(w2.amount(Currency("RUB")), 90_c);
  EXPECT_EQ(w2.amount(Currency("USD")), 0_c);

  Wallet w3 = Wallet::get(addr3, s);
  EXPECT_TRUE(w3.is_valid());
  EXPECT_TRUE(w3.address().is_valid());
  EXPECT_EQ(w3.currencies().size(), static_cast<size_t>(2));
  EXPECT_EQ(w3.amount(Currency("RUB")), 40_c);
  EXPECT_EQ(w3.amount(Currency("USD")), 0.08_c);
}

TEST_F(WalletTest, UnexistingAddress)
{
  Storage s;
  ASSERT_TRUE(s.open(path_to_tests_));
  ASSERT_TRUE(s.last_hash().is_empty());

  Pool p{s.last_hash(), 0, s};
  ASSERT_TRUE(p.add_transaction(Transaction(addr1, addr2, Currency("RUB"), 10_c), true));
  ASSERT_TRUE(p.add_transaction(Transaction(addr2, addr3, Currency("RUB"), 20_c), true));
  ASSERT_TRUE(p.add_transaction(Transaction(addr3, addr1, Currency("RUB"), 30_c), true));
  ASSERT_TRUE(p.compose());
  ASSERT_TRUE(p.save());
  ASSERT_EQ(s.last_hash(), p.hash());

  p = Pool{p.hash(), p.sequence() + 1, p.storage()};
  ASSERT_TRUE(p.compose());
  ASSERT_TRUE(p.save());
  ASSERT_EQ(s.last_hash(), p.hash());

  p = Pool{p.hash(), p.sequence() + 1, p.storage()};
  ASSERT_TRUE(p.add_transaction(Transaction(addr1, addr2, Currency("RUB"), 300_c), true));
  ASSERT_TRUE(p.add_transaction(Transaction(addr2, addr3, Currency("RUB"), 200_c), true));
  ASSERT_TRUE(p.add_transaction(Transaction(addr3, addr1, Currency("RUB"), 150_c), true));
  ASSERT_TRUE(p.compose());
  ASSERT_TRUE(p.save());
  ASSERT_EQ(s.last_hash(), p.hash());

  p = Pool{p.hash(), p.sequence() + 1, p.storage()};
  ASSERT_TRUE(p.compose());
  ASSERT_TRUE(p.save());
  ASSERT_EQ(s.last_hash(), p.hash());

  p = Pool{p.hash(), p.sequence() + 1, p.storage()};
  ASSERT_TRUE(p.add_transaction(Transaction(addr1, addr2, Currency("USD"), 0.05_c), true));
  ASSERT_TRUE(p.add_transaction(Transaction(addr2, addr3, Currency("USD"), 0.06_c), true));
  ASSERT_TRUE(p.add_transaction(Transaction(addr3, addr1, Currency("USD"), 0.07_c), true));
  ASSERT_TRUE(p.compose());
  ASSERT_TRUE(p.save());
  ASSERT_EQ(s.last_hash(), p.hash());

  p = Pool{p.hash(), p.sequence() + 1, p.storage()};
  ASSERT_TRUE(p.compose());
  ASSERT_TRUE(p.save());
  ASSERT_EQ(s.last_hash(), p.hash());

  p = Pool{p.hash(), p.sequence() + 1, p.storage()};
  ASSERT_TRUE(p.add_transaction(Transaction(addr1, addr2, Currency("USD"), 0.19_c), true));
  ASSERT_TRUE(p.add_transaction(Transaction(addr2, addr3, Currency("USD"), 0.18_c), true));
  ASSERT_TRUE(p.add_transaction(Transaction(addr3, addr1, Currency("USD"), 0.09_c), true));
  ASSERT_TRUE(p.compose());
  ASSERT_TRUE(p.save());
  ASSERT_EQ(s.last_hash(), p.hash());

  ::csdb::internal::byte_array ukey = addr1.public_key();
  EXPECT_FALSE(ukey.empty());
  ukey[0] = ~ukey[0];

  Wallet w = Wallet::get(Address::from_public_key(ukey), s);
  EXPECT_TRUE(w.is_valid());
  EXPECT_TRUE(w.address().is_valid());
  EXPECT_EQ(w.currencies().size(), static_cast<size_t>(0));
}
