#include "csdb/transaction.h"

#include <cstring>
#include <iostream>

#include <gtest/gtest.h>

#include "csdb_unit_tests_environment.h"
#include "binary_streams.h"

class TransactionIDTest : public ::testing::Test
{
};

class TransactionTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    EXPECT_TRUE(addr1.is_valid());
    EXPECT_TRUE(addr2.is_valid());
    EXPECT_TRUE(addr3.is_valid());
    EXPECT_NE(addr1, addr2);
    EXPECT_NE(addr2, addr3);
  }

protected:
  ::csdb::Address addr1 = ::csdb::Address::from_string("0000000000000000000000000000000000000000");
  ::csdb::Address addr2 = ::csdb::Address::from_string("0000000000000000000000000000000000000001");
  ::csdb::Address addr3 = ::csdb::Address::from_string("0000000000000000000000000000000000000002");
};

using namespace csdb;

TEST_F(TransactionIDTest, ToStringZeroTerminated)
{
  /// \todo Исключить использование этого конструктора TransactionID
  ::std::string str = TransactionID(PoolHash::calc_from_data({1,2,3}), 0).to_string();
  EXPECT_EQ(str.size(), strlen(str.c_str()));
}

TEST_F(TransactionIDTest, FromValidString)
{
  /// \todo Исключить использование этого конструктора TransactionID
  TransactionID id1{PoolHash::calc_from_data({1,2,3}), 0};
  TransactionID id2{PoolHash::calc_from_data({1,2,3}), 1};

  TransactionID id3 = TransactionID::from_string(id1.to_string());
  TransactionID id4 = TransactionID::from_string(id2.to_string());

  EXPECT_TRUE(id1.is_valid());
  EXPECT_TRUE(id2.is_valid());
  EXPECT_TRUE(id3.is_valid());
  EXPECT_TRUE(id4.is_valid());

  EXPECT_NE(id1.to_string(), id2.to_string());
  EXPECT_EQ(id1, id3);
  EXPECT_EQ(id2, id4);
  EXPECT_NE(id1, id2);
  EXPECT_NE(id3, id4);
}

TEST_F(TransactionIDTest, FromInvalidString)
{
  /// \todo Исключить использование этого конструктора TransactionID
  ::std::string valid = TransactionID(PoolHash::calc_from_data({1,2,3}), 0).to_string();
  auto pos = valid.find(':');
  ASSERT_NE(::std::string::npos, pos);

  {
    ::std::string invalid{valid, 0, pos};
    EXPECT_FALSE(TransactionID::from_string(invalid).is_valid());
  }

  {
    ::std::string invalid{valid, 0, pos + 1};
    EXPECT_FALSE(TransactionID::from_string(invalid).is_valid());
  }

  {
    EXPECT_FALSE(TransactionID::from_string(valid + "A").is_valid());
  }

  {
    ::std::string invalid{valid, 1};
    EXPECT_FALSE(TransactionID::from_string(invalid).is_valid());
  }
}

TEST_F(TransactionTest, SimpleCreation)
{
  EXPECT_FALSE(Transaction().is_valid());
  EXPECT_TRUE(Transaction(addr1, addr2, Currency("CS"), 1_c).is_valid());
  EXPECT_FALSE(Transaction(addr1, addr1, Currency("CS"), 1_c).is_valid());
  EXPECT_FALSE(Transaction(addr1, addr2, Currency("CS"), 0_c).is_valid());
}

TEST_F(TransactionTest, CreateAndMakeValid)
{
  Transaction t;
  EXPECT_FALSE(t.is_valid());
  t.set_source(addr1);
  EXPECT_FALSE(t.is_valid());
  t.set_target(addr2);
  EXPECT_FALSE(t.is_valid());
  t.set_currency(Currency("CS"));
  EXPECT_FALSE(t.is_valid());
  t.set_amount(0.01_c);
  EXPECT_TRUE(t.is_valid());
}

TEST_F(TransactionTest, InternalComparator)
{
  EXPECT_EQ(Transaction(), Transaction());
  EXPECT_EQ(Transaction(addr1, addr2, Currency("CS"), 1_c), Transaction(addr1, addr2, Currency("CS"), 1_c));
  EXPECT_NE(Transaction(addr1, addr2, Currency("CS"), 1_c), Transaction(addr1, addr2, Currency("CS"), 2_c));
  EXPECT_NE(Transaction(addr1, addr2, Currency("CS"), 1_c), Transaction(addr1, addr2, Currency("CS1"), 1_c));
  EXPECT_NE(Transaction(addr1, addr2, Currency("CS"), 1_c), Transaction(addr1, addr3, Currency("CS"), 1_c));

  Transaction t1(addr1, addr2, Currency("CS"), 1_c);
  Transaction t2(t1);
  EXPECT_EQ(t1, t2);
  EXPECT_TRUE(t1.add_user_field(UFID_COMMENT, "Comment"));
  EXPECT_NE(t1, t2);
  EXPECT_TRUE(t2.add_user_field(UFID_COMMENT, "Comment"));
  EXPECT_EQ(t1, t2);
  EXPECT_TRUE(t2.add_user_field(UFID_COMMENT, 123.456_c));
  EXPECT_NE(t1, t2);
}

TEST_F(TransactionTest, ToBinaryInvalid)
{
  EXPECT_TRUE(Transaction().to_binary().empty());
  EXPECT_TRUE(Transaction(addr1, addr2, Currency("CS"), 0_c).to_binary().empty());
  EXPECT_TRUE(Transaction(addr1, addr1, Currency("CS"), 0_c).to_binary().empty());
}

TEST_F(TransactionTest, ToFromBinaryValid)
{
  Transaction sample1(addr1, addr2, Currency("CS"), 1_c);
  Transaction sample2(addr2, addr1, Currency("CS"), 1_c);
  EXPECT_NE(sample1, sample2);
  ::csdb::internal::byte_array enc1 = sample1.to_binary();
  ::csdb::internal::byte_array enc2 = sample2.to_binary();
  EXPECT_FALSE(enc1.empty());
  EXPECT_FALSE(enc2.empty());
  EXPECT_NE(enc1, enc2);

  Transaction t1 = Transaction::from_binary(enc1);
  Transaction t2 = Transaction::from_binary(enc2);
  EXPECT_TRUE(t1.is_valid());
  EXPECT_TRUE(t2.is_valid());
  EXPECT_EQ(t1, sample1);
  EXPECT_EQ(t2, sample2);
}

TEST_F(TransactionTest, FromBinaryInvalid)
{
  EXPECT_FALSE(Transaction::from_binary({}).is_valid());
  EXPECT_FALSE(Transaction::from_binary({1,2,3}).is_valid());

  ::csdb::internal::byte_array valid = Transaction(addr1, addr2, Currency("CS"), 1_c).to_binary();
  EXPECT_FALSE(valid.empty());

  {
    ::csdb::internal::byte_array invalid(valid);
    invalid.resize(invalid.size() - 1);
    EXPECT_FALSE(Transaction::from_binary(invalid).is_valid());
  }

  {
    ::csdb::internal::byte_array invalid(valid);
    invalid[0] = ~invalid[0];
    EXPECT_FALSE(Transaction::from_binary(invalid).is_valid());
  }
}

TEST_F(TransactionTest, UserFieldAdd)
{
  Transaction t(addr1, addr2, Currency("CS"), 1_c);
  EXPECT_EQ(t.user_field_ids(), ::std::set<user_field_id_t>({}));
  EXPECT_TRUE(t.add_user_field(1, 100));
  EXPECT_TRUE(t.add_user_field(2, "Text"));
  EXPECT_TRUE(t.add_user_field(3, 123.456_c));
  EXPECT_EQ(t.user_field_ids(), ::std::set<user_field_id_t>({1,2,3}));
  EXPECT_EQ(t.user_field(1), UserField(100));
  EXPECT_EQ(t.user_field(2), UserField("Text"));
  EXPECT_EQ(t.user_field(3), UserField(123.456_c));
  EXPECT_EQ(t.user_field(4), UserField());
}

TEST_F(TransactionTest, UserFieldSerialize)
{
  Transaction src(addr1, addr2, Currency("CS"), 1_c);
  EXPECT_EQ(src.user_field_ids(), ::std::set<user_field_id_t>({}));
  EXPECT_TRUE(src.add_user_field(1, 100));
  EXPECT_TRUE(src.add_user_field(2, "Text"));
  EXPECT_TRUE(src.add_user_field(3, 123.456_c));
  EXPECT_EQ(src.user_field_ids(), ::std::set<user_field_id_t>({1,2,3}));
  EXPECT_EQ(src.user_field(1), UserField(100));
  EXPECT_EQ(src.user_field(2), UserField("Text"));
  EXPECT_EQ(src.user_field(3), UserField(123.456_c));
  EXPECT_EQ(src.user_field(4), UserField());

  ::csdb::internal::byte_array enc = src.to_binary();
  EXPECT_FALSE(enc.empty());

  Transaction res = Transaction::from_binary(enc);
  EXPECT_TRUE(res.is_valid());
  EXPECT_EQ(res.user_field_ids(), ::std::set<user_field_id_t>({1,2,3}));
  EXPECT_EQ(res.user_field(1), UserField(100));
  EXPECT_EQ(res.user_field(2), UserField("Text"));
  EXPECT_EQ(res.user_field(3), UserField(123.456_c));
  EXPECT_EQ(res.user_field(4), UserField());
  EXPECT_EQ(src, res);
}

TEST_F(TransactionTest, Balance)
{
  {
    Transaction t{addr1, addr2, Currency("CS"), 1.23_c};
    EXPECT_TRUE(t.is_valid());
    EXPECT_EQ(t.amount(), 1.23_c);
    EXPECT_EQ(t.balance(), 1.23_c);
    t.set_balance(4.56_c);
    EXPECT_EQ(t.amount(), 1.23_c);
    EXPECT_EQ(t.balance(), 4.56_c);
  }

  {
    Transaction t1{addr1, addr2, Currency("CS"), 1.23_c, 4.56_c};
    EXPECT_TRUE(t1.is_valid());
    EXPECT_EQ(t1.amount(), 1.23_c);
    EXPECT_EQ(t1.balance(), 4.56_c);

    Transaction t2 = Transaction::from_binary(t1.to_binary());
    EXPECT_TRUE(t2.is_valid());
    EXPECT_EQ(t2.amount(), 1.23_c);
    EXPECT_EQ(t2.balance(), 4.56_c);

    t2.set_balance(7.89_c);
    EXPECT_EQ(t2.amount(), 1.23_c);
    EXPECT_EQ(t2.balance(), 7.89_c);
    EXPECT_EQ(t1.balance(), 4.56_c);
  }
}

TEST_F(TransactionTest, PreventReadOnlyModification)
{
  Pool p{PoolHash{}, 0};
  ASSERT_TRUE(p.is_valid());
  ASSERT_TRUE(p.add_transaction(Transaction{addr1, addr2, Currency("CS"), 1_c}, true));

  Transaction t = p.transaction(0);
  ASSERT_TRUE(t.is_valid());
  ASSERT_FALSE(t.id().is_valid());

  ASSERT_TRUE(p.compose());
  t = p.transaction(0);
  EXPECT_TRUE(t.is_valid());
  EXPECT_TRUE(t.is_read_only());
  EXPECT_TRUE(t.id().is_valid());

  Amount save = t.amount();
  t.set_amount(save + 1_c);
  EXPECT_EQ(t.amount(), save);

  save = t.balance();
  t.set_balance(save + 1_c);
  EXPECT_EQ(t.balance(), save);
}

TEST_F(TransactionTest, DropIDThenAddingToPool)
{
  Pool p_src{PoolHash{}, 0};
  ASSERT_TRUE(p_src.is_valid());
  ASSERT_TRUE(p_src.add_transaction(Transaction{addr1, addr2, Currency("CS"), 1_c}, true));
  ASSERT_TRUE(p_src.compose());

  Transaction t_src = p_src.transaction(0);
  EXPECT_TRUE(t_src.is_valid());
  EXPECT_TRUE(t_src.is_read_only());
  EXPECT_TRUE(t_src.id().is_valid());

  Pool p_dst{PoolHash{}, 1};
  ASSERT_TRUE(p_dst.is_valid());
  ASSERT_TRUE(p_dst.add_transaction(t_src, true));

  Transaction t_dst = p_dst.transaction(0);
  EXPECT_TRUE(t_dst.is_valid());
  EXPECT_EQ(t_src, t_dst);
  EXPECT_FALSE(t_dst.id().is_valid());

  ASSERT_TRUE(p_dst.compose());
  t_dst = p_dst.transaction(0);
  EXPECT_TRUE(t_dst.is_valid());
  EXPECT_EQ(t_src, t_dst);
  EXPECT_TRUE(t_dst.id().is_valid());
  EXPECT_NE(t_src.id(), t_dst.id());
}
