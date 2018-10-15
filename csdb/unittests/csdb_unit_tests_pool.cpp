#include "csdb/pool.h"

#include <iostream>
#include <set>
#include <map>
#include <stdexcept>

#include <gtest/gtest.h>

#include "csdb_unit_tests_environment.h"

#include "csdb/internal/utils.h"
#include "priv_crypto.h"

class PoolHashTest : public ::testing::Test
{
};

class PoolTest : public ::testing::Test
{
protected:
  PoolTest() :
    path_to_tests_(::csdb::internal::app_data_path() + "csdb_unittests_pool")
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

::std::ostream& operator <<(::std::ostream& os, const ::csdb::PoolHash& value)
{
  os << value.to_string();
  return os;
}

using namespace csdb;

TEST_F(PoolHashTest, EmptyHash)
{
  PoolHash h;
  EXPECT_TRUE(h.is_empty());
  EXPECT_EQ(h.size(), static_cast<size_t>(0));
  EXPECT_TRUE(h.to_string().empty());
}

TEST_F(PoolHashTest, HashFromData)
{
  {
    PoolHash h = PoolHash::calc_from_data({1,2,3});
    EXPECT_FALSE(h.is_empty());
    EXPECT_EQ(h.size(), ::csdb::priv::crypto::hash_size);
    EXPECT_FALSE(h.to_string().empty());
  }

  {
    PoolHash h = PoolHash::calc_from_data({});
    EXPECT_FALSE(h.is_empty());
    EXPECT_EQ(h.size(), ::csdb::priv::crypto::hash_size);
    EXPECT_FALSE(h.to_string().empty());
  }
}

TEST_F(PoolHashTest, Compare)
{
  PoolHash h1 = PoolHash::calc_from_data({1,2,3});
  PoolHash h2 = PoolHash::calc_from_data({1,2,3});
  PoolHash h3 = PoolHash::calc_from_data({1,2,3,4});
  PoolHash h4;
  EXPECT_EQ(h1, h2);
  EXPECT_NE(h1, h3);
  EXPECT_NE(h1, h4);
  EXPECT_NE(h2, h3);
  EXPECT_NE(h2, h4);
  EXPECT_NE(h3, h4);
}

TEST_F(PoolHashTest, StdSet)
{
  ::std::set<PoolHash> hs;
  EXPECT_TRUE(hs.insert(PoolHash::calc_from_data({1,2,3})).second);
  EXPECT_FALSE(hs.insert(PoolHash::calc_from_data({1,2,3})).second);
  EXPECT_TRUE(hs.insert(PoolHash::calc_from_data({1,2,4})).second);
  EXPECT_TRUE(hs.insert(PoolHash{}).second);
  EXPECT_FALSE(hs.insert(PoolHash{}).second);

  EXPECT_EQ(hs.size(), static_cast<size_t>(3));
  EXPECT_EQ(hs.count(PoolHash::calc_from_data({1,2,3})), static_cast<size_t>(1));
  EXPECT_EQ(hs.count(PoolHash::calc_from_data({1,2,4})), static_cast<size_t>(1));
  EXPECT_EQ(hs.count(PoolHash::calc_from_data({1,4,2})), static_cast<size_t>(0));
  EXPECT_EQ(hs.count(PoolHash{}), static_cast<size_t>(1));

  EXPECT_EQ(hs.erase(PoolHash::calc_from_data({1,2,3})), static_cast<size_t>(1));
  EXPECT_EQ(hs.erase(PoolHash::calc_from_data({1,2,3})), static_cast<size_t>(0));
  EXPECT_EQ(hs.size(), static_cast<size_t>(2));
  EXPECT_EQ(hs.count(PoolHash::calc_from_data({1,2,3})), static_cast<size_t>(0));
  EXPECT_EQ(hs.count(PoolHash::calc_from_data({1,2,4})), static_cast<size_t>(1));
  EXPECT_EQ(hs.count(PoolHash::calc_from_data({1,4,2})), static_cast<size_t>(0));
  EXPECT_EQ(hs.count(PoolHash{}), static_cast<size_t>(1));
}

TEST_F(PoolHashTest, StdMap)
{
  ::std::map<PoolHash, ::internal::byte_array> hm;
  EXPECT_TRUE(hm.emplace(PoolHash::calc_from_data({1,2,3}), internal::byte_array{1,2,3}).second);
  EXPECT_FALSE(hm.emplace(PoolHash::calc_from_data({1,2,3}), internal::byte_array{1,3,2}).second);
  EXPECT_TRUE(hm.emplace(PoolHash::calc_from_data({1,3,2}), internal::byte_array{1,3,2}).second);
  EXPECT_TRUE(hm.emplace(PoolHash{}, internal::byte_array{}).second);

  EXPECT_EQ(hm.size(), static_cast<size_t>(3));
  EXPECT_EQ(hm.at(PoolHash::calc_from_data({1,2,3})), (internal::byte_array{1,2,3}));
  EXPECT_EQ(hm.at(PoolHash::calc_from_data({1,3,2})), (internal::byte_array{1,3,2}));
  EXPECT_THROW(hm.at(PoolHash::calc_from_data({1,2,4})), ::std::out_of_range);
  EXPECT_EQ(hm.at(PoolHash{}), (internal::byte_array{}));

  EXPECT_EQ(hm.erase(PoolHash::calc_from_data({1,2,3})), static_cast<size_t>(1));
  EXPECT_EQ(hm.erase(PoolHash::calc_from_data({1,2,3})), static_cast<size_t>(0));
  EXPECT_EQ(hm.size(), static_cast<size_t>(2));
  EXPECT_THROW(hm.at(PoolHash::calc_from_data({1,2,3})), ::std::out_of_range);
  EXPECT_EQ(hm.at(PoolHash::calc_from_data({1,3,2})), (internal::byte_array{1,3,2}));
  EXPECT_THROW(hm.at(PoolHash::calc_from_data({1,2,4})), std::out_of_range);
  EXPECT_EQ(hm.at(PoolHash{}), (internal::byte_array{}));
}

TEST_F(PoolHashTest, FromValidString)
{
  {
    PoolHash h1;
    EXPECT_TRUE(h1.to_string().empty());
    PoolHash h2 = PoolHash::from_string(h1.to_string());
    EXPECT_TRUE(h1.is_empty());
    EXPECT_TRUE(h2.is_empty());
    EXPECT_EQ(h1, h2);
  }

  {
    PoolHash h1 = PoolHash::calc_from_data({1,2,3});
    PoolHash h2 = PoolHash::from_string(h1.to_string());
    PoolHash h3 = PoolHash::calc_from_data({1,3,2});
    PoolHash h4 = PoolHash::from_string(h3.to_string());
    EXPECT_FALSE(h1.is_empty());
    EXPECT_FALSE(h2.is_empty());
    EXPECT_FALSE(h3.is_empty());
    EXPECT_FALSE(h4.is_empty());
    EXPECT_EQ(h1, h2);
    EXPECT_EQ(h3, h4);
    EXPECT_NE(h1, h3);
    EXPECT_NE(h2, h4);
  }
}

TEST_F(PoolHashTest, FromValidBinary)
{
  {
    PoolHash h1;
    EXPECT_TRUE(h1.to_binary().empty());
    PoolHash h2 = PoolHash::from_binary(h1.to_binary());
    EXPECT_TRUE(h1.is_empty());
    EXPECT_TRUE(h2.is_empty());
    EXPECT_EQ(h1, h2);
  }

  {
    PoolHash h1 = PoolHash::calc_from_data({1,2,3});
    PoolHash h2 = PoolHash::from_binary(h1.to_binary());
    PoolHash h3 = PoolHash::calc_from_data({1,3,2});
    PoolHash h4 = PoolHash::from_binary(h3.to_binary());
    EXPECT_FALSE(h1.is_empty());
    EXPECT_FALSE(h2.is_empty());
    EXPECT_FALSE(h3.is_empty());
    EXPECT_FALSE(h4.is_empty());
    EXPECT_EQ(h1, h2);
    EXPECT_EQ(h3, h4);
    EXPECT_NE(h1, h3);
    EXPECT_NE(h2, h4);
  }
}

TEST_F(PoolHashTest, FromInvalidString)
{
  EXPECT_TRUE(::PoolHash::from_string("Invalid string").is_empty());

  ::std::string valid = PoolHash::calc_from_data({1,2,3}).to_string();

  {
    ::std::string invalid(valid);
    invalid.insert(invalid.begin(), 'Q');
    EXPECT_TRUE(::PoolHash::from_string(invalid).is_empty());
  }

  {
    ::std::string invalid(valid);
    invalid += "00";
    EXPECT_TRUE(::PoolHash::from_string(invalid).is_empty());
  }

  {
    ::std::string invalid(valid);
    invalid.erase(0, 1);
    EXPECT_TRUE(::PoolHash::from_string(invalid).is_empty());
  }
}

TEST_F(PoolTest, Empty)
{
  {
    Pool p;
    EXPECT_FALSE(p.is_valid());
    EXPECT_FALSE(p.is_read_only());
    EXPECT_TRUE(p.previous_hash().is_empty());
    EXPECT_TRUE(p.hash().is_empty());
    EXPECT_TRUE(p.to_binary().empty());
    EXPECT_EQ(p.transactions_count(), static_cast<size_t>(0));
  }

  {
    Pool p(PoolHash(), 0);
    EXPECT_TRUE(p.is_valid());
    EXPECT_FALSE(p.is_read_only());
    EXPECT_TRUE(p.previous_hash().is_empty());
    EXPECT_TRUE(p.hash().is_empty());
    EXPECT_TRUE(p.to_binary().empty());
    EXPECT_EQ(p.transactions_count(), static_cast<size_t>(0));
    EXPECT_EQ(p.sequence(), static_cast<Pool::sequence_t>(0));
  }

  {
    Pool p(PoolHash::calc_from_data({}), 1);
    EXPECT_TRUE(p.is_valid());
    EXPECT_FALSE(p.is_read_only());
    EXPECT_EQ(p.previous_hash(), PoolHash::calc_from_data({}));
    EXPECT_TRUE(p.hash().is_empty());
    EXPECT_TRUE(p.to_binary().empty());
    EXPECT_EQ(p.transactions_count(), static_cast<size_t>(0));
    EXPECT_EQ(p.sequence(), static_cast<Pool::sequence_t>(1));
  }
}

TEST_F(PoolTest, MakeValid)
{
  {
    Pool p;
    EXPECT_FALSE(p.is_valid());
    EXPECT_FALSE(p.is_read_only());

    p.set_sequence(0);
    EXPECT_TRUE(p.is_valid());
    EXPECT_FALSE(p.is_read_only());
    EXPECT_TRUE(p.previous_hash().is_empty());
    EXPECT_TRUE(p.hash().is_empty());
    EXPECT_TRUE(p.to_binary().empty());
    EXPECT_EQ(p.transactions_count(), static_cast<size_t>(0));
    EXPECT_EQ(p.sequence(), static_cast<Pool::sequence_t>(0));
  }

  {
    Pool p;
    EXPECT_FALSE(p.is_valid());
    EXPECT_FALSE(p.is_read_only());

    p.set_previous_hash(PoolHash::calc_from_data({}));
    EXPECT_TRUE(p.is_valid());
    EXPECT_FALSE(p.is_read_only());
    EXPECT_EQ(p.previous_hash(), PoolHash::calc_from_data({}));
    EXPECT_TRUE(p.hash().is_empty());
    EXPECT_TRUE(p.to_binary().empty());
    EXPECT_EQ(p.transactions_count(), static_cast<size_t>(0));
    EXPECT_EQ(p.sequence(), static_cast<Pool::sequence_t>(0));
  }

  {
    Pool p;
    EXPECT_FALSE(p.is_valid());
    EXPECT_FALSE(p.is_read_only());

    p.set_storage(Storage());
    EXPECT_TRUE(p.is_valid());
    EXPECT_FALSE(p.is_read_only());
    EXPECT_EQ(p.previous_hash(), PoolHash{});
    EXPECT_TRUE(p.hash().is_empty());
    EXPECT_TRUE(p.to_binary().empty());
    EXPECT_EQ(p.transactions_count(), static_cast<size_t>(0));
    EXPECT_EQ(p.sequence(), static_cast<Pool::sequence_t>(0));
  }

  {
    Pool p;
    EXPECT_FALSE(p.is_valid());
    EXPECT_FALSE(p.is_read_only());

    EXPECT_TRUE(p.add_user_field(UFID_COMMENT, "Comment"));
    EXPECT_TRUE(p.is_valid());
    EXPECT_FALSE(p.is_read_only());
    EXPECT_EQ(p.previous_hash(), PoolHash{});
    EXPECT_TRUE(p.hash().is_empty());
    EXPECT_TRUE(p.to_binary().empty());
    EXPECT_EQ(p.transactions_count(), static_cast<size_t>(0));
    EXPECT_EQ(p.sequence(), static_cast<Pool::sequence_t>(0));
  }
}

TEST_F(PoolTest, SetReadOnlyAfterCompose)
{
  Pool p(PoolHash::calc_from_data({1}), 1);
  Transaction t{addr1, addr2, Currency("CS"), 1_c};
  EXPECT_TRUE(p.is_valid());
  EXPECT_FALSE(p.is_read_only());
  EXPECT_TRUE(p.add_transaction(t, true));
  EXPECT_EQ(p.transactions_count(), static_cast<size_t>(1));

  EXPECT_TRUE(p.compose());
  EXPECT_TRUE(p.is_valid());
  EXPECT_TRUE(p.is_read_only());

  p.set_sequence(p.sequence() + 1);
  EXPECT_EQ(p.sequence(), static_cast<Pool::sequence_t>(1));

  p.set_previous_hash(PoolHash::calc_from_data({2}));
  EXPECT_EQ(p.previous_hash(), PoolHash::calc_from_data({1}));

  EXPECT_FALSE(p.add_transaction(t, true));
  EXPECT_EQ(p.transactions_count(), static_cast<size_t>(1));
}

TEST_F(PoolTest, ToFromBinaryValidEmpty)
{
  {
    Pool src(PoolHash(), 0);
    EXPECT_TRUE(src.is_valid());
    EXPECT_FALSE(src.is_read_only());

    EXPECT_TRUE(src.compose());
    EXPECT_TRUE(src.is_valid());
    EXPECT_TRUE(src.is_read_only());

    Pool dst = Pool::from_binary(src.to_binary());
    EXPECT_TRUE(dst.is_valid());
    EXPECT_TRUE(dst.is_read_only());
    EXPECT_EQ(src, dst);
  }

  {
    Pool src(PoolHash::calc_from_data({1}), 1);
    EXPECT_TRUE(src.is_valid());
    EXPECT_FALSE(src.is_read_only());

    EXPECT_TRUE(src.compose());
    EXPECT_TRUE(src.is_valid());
    EXPECT_TRUE(src.is_read_only());

    Pool dst = Pool::from_binary(src.to_binary());
    EXPECT_EQ(src, dst);
  }
}

TEST_F(PoolTest, FromBinaryInvalidEmpty)
{
  EXPECT_FALSE(Pool::from_binary({}).is_valid());
  EXPECT_FALSE(Pool::from_binary({1,2,3}).is_valid());

  Pool src(PoolHash::calc_from_data({1}), 1);
  EXPECT_TRUE(src.compose());
  ::csdb::internal::byte_array valid = src.to_binary();
  EXPECT_FALSE(valid.empty());

  {
    ::csdb::internal::byte_array invalid(valid);
    invalid.resize(invalid.size() - 1);
    EXPECT_FALSE(Pool::from_binary(invalid).is_valid());
  }

  {
    ::csdb::internal::byte_array invalid(valid);
    invalid[0] = ~invalid[0];
    EXPECT_FALSE(Pool::from_binary(invalid).is_valid());
  }
}

TEST_F(PoolTest, ErrorSaveInvalidOrUncomposed)
{
  Storage s;
  ASSERT_TRUE(s.open(path_to_tests_));
  ASSERT_TRUE(s.last_hash().is_empty());

  {
    Pool p;
    EXPECT_FALSE(p.save(s));
  }

  {
    Pool p;
    EXPECT_FALSE(p.compose());
    EXPECT_FALSE(p.save(s));
  }

  {
    Pool p(PoolHash{}, 0, s);
    EXPECT_TRUE(p.is_valid());
    EXPECT_FALSE(p.save(s));
  }
}

TEST_F(PoolTest, SaveLoadEmpty)
{
  Storage s;
  ASSERT_TRUE(s.open(path_to_tests_));
  ASSERT_TRUE(s.last_hash().is_empty());

  Pool src1(PoolHash{}, 0, s);
  EXPECT_TRUE(src1.compose());
  EXPECT_FALSE(src1.hash().is_empty());
  EXPECT_TRUE(src1.save());
  EXPECT_EQ(src1.hash(), s.last_hash());

  Pool src2(src1.hash(), src1.sequence() + 1, src1.storage());
  EXPECT_TRUE(src2.compose());
  EXPECT_FALSE(src2.hash().is_empty());
  EXPECT_TRUE(src2.save());
  EXPECT_EQ(src2.hash(), s.last_hash());

  Pool res1 = Pool::load(src1.hash(), s);
  Pool res2 = Pool::load(src2.hash(), s);
  EXPECT_TRUE(res1.is_valid());
  EXPECT_TRUE(res2.is_valid());
  EXPECT_TRUE(res1.is_read_only());
  EXPECT_TRUE(res2.is_read_only());
  EXPECT_EQ(src1, res1);
  EXPECT_EQ(src2, res2);
}

TEST_F(PoolTest, SaveEmptyDuplicate)
{
  Storage s;
  ASSERT_TRUE(s.open(path_to_tests_));
  ASSERT_TRUE(s.last_hash().is_empty());

  Pool p(PoolHash{}, 0, s);
  EXPECT_TRUE(p.compose());
  EXPECT_FALSE(p.hash().is_empty());
  EXPECT_TRUE(p.save());
  EXPECT_EQ(p.hash(), s.last_hash());

  EXPECT_FALSE(p.save());
  EXPECT_EQ(s.last_error(), Storage::InvalidParameter);
}

TEST_F(PoolTest, SaveLoadNotFound)
{
  Storage s;
  ASSERT_TRUE(s.open(path_to_tests_));
  ASSERT_TRUE(s.last_hash().is_empty());

  Pool p(PoolHash{}, 0, s);
  EXPECT_TRUE(p.compose());
  EXPECT_FALSE(p.hash().is_empty());
  EXPECT_TRUE(p.save());
  EXPECT_EQ(p.hash(), s.last_hash());

  ::csdb::internal::byte_array hash_binary = p.hash().to_binary();
  hash_binary[0] = ~hash_binary[0];
  PoolHash hash = PoolHash::from_binary(hash_binary);
  Pool res = Pool::load(hash, s);
  EXPECT_FALSE(res.is_valid());
  EXPECT_EQ(s.last_error(), Storage::DatabaseError);
  EXPECT_EQ(s.db_last_error(), Database::NotFound);
}

TEST_F(PoolTest, FromToBinaryWithTransactions)
{
  Pool src{PoolHash{}, 0};
  EXPECT_TRUE(src.is_valid());

  EXPECT_TRUE(src.add_transaction(Transaction(addr1, addr2, Currency("RUB"), 1_c), true));
  EXPECT_TRUE(src.add_transaction(Transaction(addr2, addr3, Currency("RUB"), 2_c), true));
  EXPECT_TRUE(src.add_transaction(Transaction(addr3, addr1, Currency("RUB"), 3_c), true));

  EXPECT_EQ(src.transactions_count(), static_cast<size_t>(3));
  for (size_t i = 0; i < src.transactions_count(); ++i) {
    Transaction t = src.transaction(i);
    EXPECT_TRUE(t.is_valid());
    EXPECT_FALSE(t.id().is_valid());
  }

  EXPECT_TRUE(src.compose());
  EXPECT_TRUE(src.is_read_only());
  EXPECT_EQ(src.transactions_count(), static_cast<size_t>(3));
  for (size_t i = 0; i < src.transactions_count(); ++i) {
    Transaction t = src.transaction(i);
    EXPECT_TRUE(t.is_valid());
    EXPECT_TRUE(t.id().is_valid());
    EXPECT_EQ(t.id().pool_hash(), src.hash());
  }

  Pool dst = Pool::from_binary(src.to_binary());
  EXPECT_TRUE(dst.is_valid());
  EXPECT_TRUE(dst.is_read_only());
  EXPECT_EQ(src, dst);

  for (size_t i = 0; i < dst.transactions_count(); ++i) {
    Transaction t = dst.transaction(src.transaction(i).id());
    EXPECT_TRUE(t.is_valid());
    EXPECT_TRUE(t.id().is_valid());
    EXPECT_EQ(t.id().pool_hash(), dst.hash());
    EXPECT_EQ(t.id(), src.transaction(i).id());
  }
}

TEST_F(PoolTest, UserFieldCompare)
{
  Pool p1{PoolHash{}, 0}, p2{PoolHash{}, 0};
  EXPECT_EQ(p1, p2);
  EXPECT_TRUE(p1.add_user_field(UFID_COMMENT, "Comment"));
  EXPECT_NE(p1, p2);
  EXPECT_TRUE(p2.add_user_field(UFID_COMMENT, "Comment"));
  EXPECT_EQ(p1, p2);
  EXPECT_FALSE(p1.add_user_field(UFID_COMMENT, UserField{}));
  EXPECT_EQ(p1, p2);
  EXPECT_TRUE(p1.add_user_field(1, 123.456_c));
  EXPECT_NE(p1, p2);
  EXPECT_TRUE(p2.add_user_field(1, 123.456_c));
  EXPECT_EQ(p1, p2);
  EXPECT_TRUE(p1.add_user_field(UFID_COMMENT, 100));
  EXPECT_NE(p1, p2);
  EXPECT_TRUE(p2.add_user_field(UFID_COMMENT, 200));
  EXPECT_NE(p1, p2);
  EXPECT_TRUE(p2.add_user_field(UFID_COMMENT, 100));
  EXPECT_EQ(p1, p2);
}

TEST_F(PoolTest, UserFieldAdd)
{
  Pool p{PoolHash{}, 0};
  EXPECT_EQ(p.user_field_ids(), ::std::set<user_field_id_t>({}));
  EXPECT_TRUE(p.add_user_field(1, 100));
  EXPECT_TRUE(p.add_user_field(2, "Text"));
  EXPECT_TRUE(p.add_user_field(3, 123.456_c));
  EXPECT_EQ(p.user_field_ids(), ::std::set<user_field_id_t>({1,2,3}));
  EXPECT_EQ(p.user_field(1), UserField(100));
  EXPECT_EQ(p.user_field(2), UserField("Text"));
  EXPECT_EQ(p.user_field(3), UserField(123.456_c));
  EXPECT_EQ(p.user_field(4), UserField());
}

TEST_F(PoolTest, UserFieldSerialize)
{
  Pool src{PoolHash{}, 0};
  EXPECT_EQ(src.user_field_ids(), ::std::set<user_field_id_t>({}));
  EXPECT_TRUE(src.add_user_field(1, 100));
  EXPECT_TRUE(src.add_user_field(2, "Text"));
  EXPECT_TRUE(src.add_user_field(3, 123.456_c));
  EXPECT_EQ(src.user_field_ids(), ::std::set<user_field_id_t>({1,2,3}));
  EXPECT_EQ(src.user_field(1), UserField(100));
  EXPECT_EQ(src.user_field(2), UserField("Text"));
  EXPECT_EQ(src.user_field(3), UserField(123.456_c));
  EXPECT_EQ(src.user_field(4), UserField());

  ASSERT_TRUE(src.compose());
  ::csdb::internal::byte_array enc = src.to_binary();
  EXPECT_FALSE(enc.empty());

  Pool res = Pool::from_binary(enc);
  EXPECT_TRUE(res.is_valid());
  EXPECT_EQ(res.user_field_ids(), ::std::set<user_field_id_t>({1,2,3}));
  EXPECT_EQ(res.user_field(1), UserField(100));
  EXPECT_EQ(res.user_field(2), UserField("Text"));
  EXPECT_EQ(res.user_field(3), UserField(123.456_c));
  EXPECT_EQ(res.user_field(4), UserField());
  EXPECT_EQ(src, res);
}

TEST_F(PoolTest, UserFieldBlockAddForToOnly)
{
  Pool p_src{PoolHash{}, 0};
  EXPECT_TRUE(p_src.add_user_field(UFID_COMMENT, "Pool Comment"));
  EXPECT_EQ(p_src.user_field_ids(), ::std::set<user_field_id_t>({UFID_COMMENT}));
  EXPECT_EQ(p_src.user_field(UFID_COMMENT), UserField("Pool Comment"));

  Transaction t_src{addr1, addr2, Currency("CS"), 1_c};
  EXPECT_TRUE(t_src.add_user_field(UFID_COMMENT, "Transaction Comment"));
  EXPECT_EQ(t_src.user_field_ids(), ::std::set<user_field_id_t>({UFID_COMMENT}));
  EXPECT_EQ(t_src.user_field(UFID_COMMENT), UserField("Transaction Comment"));
  EXPECT_TRUE(p_src.add_transaction(t_src, true));

  EXPECT_TRUE(p_src.compose());
  EXPECT_FALSE(p_src.add_user_field(UFID_COMMENT, 1));
  EXPECT_FALSE(p_src.add_user_field(1, 1_c));
  EXPECT_EQ(p_src.user_field_ids(), ::std::set<user_field_id_t>({UFID_COMMENT}));
  EXPECT_EQ(p_src.user_field(UFID_COMMENT), UserField("Pool Comment"));

  Pool p_res = Pool::from_binary(p_src.to_binary());
  EXPECT_TRUE(p_res.is_valid());
  EXPECT_TRUE(p_res.is_read_only());
  EXPECT_EQ(p_src, p_res);

  EXPECT_EQ(p_res.transactions_count(), static_cast<size_t>(1));
  Transaction t_res = p_res.transaction(0);
  EXPECT_TRUE(t_res.is_valid());
  EXPECT_TRUE(t_res.is_read_only());
  EXPECT_EQ(t_src, t_res);

  EXPECT_FALSE(p_res.add_user_field(1, 1));
  EXPECT_FALSE(t_res.add_user_field(1, 1));
  EXPECT_EQ(p_src, p_res);
  EXPECT_EQ(t_src, t_res);
}

//
// Get transaction by source, target
//

TEST_F(PoolTest, GetTransactionBySource)
{
  Pool pool{PoolHash{}, 0};
  ASSERT_TRUE(pool.is_valid());
  ASSERT_FALSE(pool.is_read_only());

  // Should work with non read-only pool

  Transaction t0{addr1, addr2, Currency("CS"), 12_c};
  ASSERT_TRUE(pool.add_transaction(t0, true));

  Transaction t1{addr1, addr3, Currency("CS"), 13_c};
  ASSERT_TRUE(pool.add_transaction(t1, true));

  EXPECT_TRUE(pool.get_last_by_source(addr1).is_valid());
  EXPECT_FALSE(pool.get_last_by_source(addr2).is_valid());

  // And on read-only as well

  ASSERT_TRUE(pool.compose());
  ASSERT_TRUE(pool.is_valid());
  ASSERT_TRUE(pool.is_read_only());

  EXPECT_TRUE(pool.get_last_by_source(addr1).is_valid());
  EXPECT_FALSE(pool.get_last_by_source(addr2).is_valid());

  // Case if source appears multiple times, should return last transaction 

  EXPECT_EQ(pool.get_last_by_source(addr1).amount(), 13_c);
}

TEST_F(PoolTest, GetTransactionByTarget)
{
  Pool pool{PoolHash{}, 0};
  ASSERT_TRUE(pool.is_valid());
  ASSERT_FALSE(pool.is_read_only());

  // Should work with non read-only pool

  Transaction t0{addr1, addr2, Currency("CS"), 12_c};
  ASSERT_TRUE(pool.add_transaction(t0, true));

  Transaction t1{addr3, addr2, Currency("CS"), 32_c};
  ASSERT_TRUE(pool.add_transaction(t1, true));

  EXPECT_TRUE(pool.get_last_by_target(addr2).is_valid());
  EXPECT_FALSE(pool.get_last_by_target(addr1).is_valid());

  // And on read-only as well

  ASSERT_TRUE(pool.compose());
  ASSERT_TRUE(pool.is_valid());
  ASSERT_TRUE(pool.is_read_only());

  EXPECT_TRUE(pool.get_last_by_target(addr2).is_valid());
  EXPECT_FALSE(pool.get_last_by_target(addr1).is_valid());

  // Case if target appears multiple times, should return last transaction

  EXPECT_EQ(pool.get_last_by_target(addr2).amount(), 32_c);
}