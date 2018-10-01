#include "csdb/storage.h"

#include "csdb_unit_tests_environment.h"

#include "csdb/wallet.h"
#include "csdb/internal/utils.h"

using namespace csdb;

class StorageTest : public ::testing::Test
{
};

class StorageTestNotOpen : public ::testing::Test
{
};

class StorageTestEmpty : public ::testing::Test
{
protected:
  StorageTestEmpty() :
    path_to_tests(internal::app_data_path() + "transaction_unittests")
  {
  }

  void SetUp() override
  {
    ASSERT_TRUE(addr1.is_valid());
    ASSERT_TRUE(addr2.is_valid());
    ASSERT_TRUE(addr3.is_valid());
  }

  void TearDown() override
  {
    ASSERT_TRUE(internal::path_remove(path_to_tests));
  }

  std::string path_to_tests;
  ::csdb::Address addr1 = ::csdb::Address::from_string("0000000000000000000000000000000000000000");
  ::csdb::Address addr2 = ::csdb::Address::from_string("0000000000000000000000000000000000000001");
  ::csdb::Address addr3 = ::csdb::Address::from_string("0000000000000000000000000000000000000002");
};

TEST_F(StorageTestNotOpen, LastError)
{
  ::csdb::Storage s;
  EXPECT_EQ(s.last_error(), ::csdb::Storage::NoError);
  EXPECT_FALSE(s.last_error_message().empty());
  EXPECT_EQ(s.db_last_error(), ::csdb::Database::NotOpen);
  EXPECT_FALSE(s.db_last_error_message().empty());
}

TEST_F(StorageTestNotOpen, FailedOpen)
{
  ::csdb::Storage s;
  EXPECT_EQ(s.last_error(), ::csdb::Storage::NoError);
  EXPECT_FALSE(s.open("/dev/null"));
  EXPECT_FALSE(s.isOpen());
  EXPECT_EQ(s.last_error(), ::csdb::Storage::DatabaseError);
  EXPECT_EQ(s.db_last_error(), ::csdb::Database::IOError);
}

TEST_F(StorageTestNotOpen, FailedOpenWithEmptyOptions)
{
  ::csdb::Storage s;
  EXPECT_FALSE(s.isOpen());
  EXPECT_FALSE(s.open(::csdb::Storage::OpenOptions{}));
  EXPECT_FALSE(s.isOpen());
  EXPECT_EQ(s.last_error(), ::csdb::Storage::DatabaseError);
  EXPECT_EQ(s.db_last_error(), ::csdb::Database::NotOpen);

  s = ::csdb::Storage::get(::csdb::Storage::OpenOptions{});
  EXPECT_FALSE(s.isOpen());
  EXPECT_EQ(s.last_error(), ::csdb::Storage::DatabaseError);
  EXPECT_EQ(s.db_last_error(), ::csdb::Database::NotOpen);
}

TEST_F(StorageTestNotOpen, CopyAndAssignment)
{
  ::csdb::Storage s1, s2;
  EXPECT_EQ(s1.last_error(), ::csdb::Storage::NoError);
  EXPECT_FALSE(s1.last_error_message().empty());
  EXPECT_EQ(s1.db_last_error(), ::csdb::Database::NotOpen);
  EXPECT_FALSE(s1.db_last_error_message().empty());

  EXPECT_EQ(s2.last_error(), ::csdb::Storage::NoError);
  EXPECT_FALSE(s2.last_error_message().empty());
  EXPECT_EQ(s2.db_last_error(), ::csdb::Database::NotOpen);
  EXPECT_FALSE(s2.db_last_error_message().empty());

  Storage s3(s1), s4;
  EXPECT_FALSE(s3.open("/dev/null"));
  EXPECT_EQ(s1.last_error(), ::csdb::Storage::DatabaseError);
  EXPECT_EQ(s1.db_last_error(), ::csdb::Database::IOError);

  EXPECT_EQ(s4.last_error(), ::csdb::Storage::NoError);
  EXPECT_FALSE(s4.last_error_message().empty());
  EXPECT_EQ(s4.db_last_error(), ::csdb::Database::NotOpen);
  EXPECT_FALSE(s4.db_last_error_message().empty());

  s4 = s1;
  EXPECT_EQ(s4.last_error(), ::csdb::Storage::DatabaseError);
  EXPECT_EQ(s4.db_last_error(), ::csdb::Database::IOError);

  EXPECT_EQ(s2.last_error(), ::csdb::Storage::NoError);
  EXPECT_FALSE(s2.last_error_message().empty());
  EXPECT_EQ(s2.db_last_error(), ::csdb::Database::NotOpen);
  EXPECT_FALSE(s2.db_last_error_message().empty());

  s4 = ::csdb::Storage();
  EXPECT_EQ(s4.last_error(), ::csdb::Storage::NoError);
  EXPECT_FALSE(s4.last_error_message().empty());
  EXPECT_EQ(s4.db_last_error(), ::csdb::Database::NotOpen);
  EXPECT_FALSE(s4.db_last_error_message().empty());

  EXPECT_EQ(s1.last_error(), ::csdb::Storage::DatabaseError);
  EXPECT_EQ(s1.db_last_error(), ::csdb::Database::IOError);
}

TEST_F(StorageTest, OpenDefaultLocation)
{
  bool callback_called = false;
  // We need to use callback to prevent rescan of potetialy existing default database
  auto callback = [&callback_called](const Storage::OpenProgress&)
  {
    callback_called = true;
    return true;
  };

  ::csdb::Storage s;
  EXPECT_TRUE(s.open(std::string{}, callback));
  if (callback_called) {
    EXPECT_FALSE(s.isOpen());
    EXPECT_EQ(s.last_error(), ::csdb::Storage::UserCancelled);
  }
  else {
    EXPECT_TRUE(s.isOpen());
    EXPECT_EQ(s.last_error(), ::csdb::Storage::NoError);
    EXPECT_EQ(s.db_last_error(), ::csdb::Database::NoError);
  }

  s.close();
  callback_called = false;
  s = ::csdb::Storage::get(std::string{}, callback);
  if (callback_called) {
    EXPECT_FALSE(s.isOpen());
    EXPECT_EQ(s.last_error(), ::csdb::Storage::UserCancelled);
    callback_called = false;
  }
  else {
    EXPECT_TRUE(s.isOpen());
    EXPECT_EQ(s.last_error(), ::csdb::Storage::NoError);
    EXPECT_EQ(s.db_last_error(), ::csdb::Database::NoError);
  }
}

TEST_F(StorageTestEmpty, WeakPtr)
{
  ::std::shared_ptr<Storage> storage = ::std::make_shared<Storage>();
  EXPECT_TRUE(storage->open(path_to_tests));
  EXPECT_TRUE(storage->isOpen());

  Storage::WeakPtr wp = storage->weak_ptr();
  ASSERT_FALSE(wp.expired());
  {
    Storage s(wp);
    EXPECT_TRUE(s.isOpen());
  }

  storage->close();
  ASSERT_FALSE(wp.expired());
  {
    Storage s(wp);
    EXPECT_FALSE(s.isOpen());
  }

  storage.reset();
  ASSERT_TRUE(wp.expired());
  {
    Storage s(wp);
    EXPECT_FALSE(s.isOpen());
    EXPECT_EQ(s.last_error(), Storage::NoError);
  }
};

TEST_F(StorageTestEmpty, RetrieveTransaction)
{
  Storage s;
  ASSERT_TRUE(s.open(path_to_tests));
  ASSERT_TRUE(s.last_hash().is_empty());

  Pool p1{PoolHash{}, 0};
  ASSERT_TRUE(p1.add_transaction(Transaction(addr1, addr2, Currency("RUB"), 10_c), true));
  ASSERT_TRUE(p1.add_transaction(Transaction(addr2, addr3, Currency("RUB"), 20_c), true));
  ASSERT_TRUE(p1.add_transaction(Transaction(addr3, addr1, Currency("RUB"), 30_c), true));
  ASSERT_TRUE(p1.compose());

  ::std::map<TransactionID, Transaction> trans1;
  for (size_t i = 0; i < p1.transactions_count(); ++i) {
    Transaction t = p1.transaction(i);
    trans1.emplace(t.id(), t);
  }

  Pool p2{p1.hash(), 1};
  ASSERT_TRUE(p2.add_transaction(Transaction(addr1, addr2, Currency("RUB"), 1.01_c), true));
  ASSERT_TRUE(p2.add_transaction(Transaction(addr2, addr3, Currency("RUB"), 1.02_c), true));
  ASSERT_TRUE(p2.add_transaction(Transaction(addr3, addr1, Currency("RUB"), 1.03_c), true));
  ASSERT_TRUE(p2.compose());

  ::std::map<TransactionID, Transaction> trans2;
  for (size_t i = 0; i < p2.transactions_count(); ++i) {
    Transaction t = p2.transaction(i);
    trans2.emplace(t.id(), t);
  }

  // Пока не записан ни один пул - все результаты должны быть инвалидны
  for (auto& it : trans1) {
    Transaction t = s.transaction(it.first);
    EXPECT_FALSE(t.is_valid());
  }

  for (auto& it : trans2) {
    Transaction t = s.transaction(it.first);
    EXPECT_FALSE(t.is_valid());
  }

  // Запишем первый пул
  ASSERT_TRUE(s.pool_save(p1));
  for (auto& it : trans1) {
    Transaction t = s.transaction(it.first);
    EXPECT_TRUE(t.is_valid());
    EXPECT_EQ(t, it.second);
  }

  for (auto& it : trans2) {
    Transaction t = s.transaction(it.first);
    EXPECT_FALSE(t.is_valid());
  }

  // Запишем второй пул
  ASSERT_TRUE(s.pool_save(p2));
  for (auto& it : trans1) {
    Transaction t = s.transaction(it.first);
    EXPECT_TRUE(t.is_valid());
    EXPECT_EQ(t, it.second);
  }

  for (auto& it : trans2) {
    Transaction t = s.transaction(it.first);
    EXPECT_TRUE(t.is_valid());
    EXPECT_EQ(t, it.second);
  }
}

//
// Get by source & target
//

TEST_F(StorageTestEmpty, GetTransactionBySourceAndTarget)
{
  Storage s;
  ASSERT_TRUE(s.open(path_to_tests));
  ASSERT_TRUE(s.last_hash().is_empty());

  Pool p1{PoolHash{}, 0};
  ASSERT_TRUE(p1.add_transaction(Transaction(addr1, addr2, Currency("RUB"), 112_c), true));
  ASSERT_TRUE(p1.add_transaction(Transaction(addr1, addr3, Currency("RUB"), 113_c), true));
  ASSERT_TRUE(p1.compose());

  Pool p2{p1.hash(), 1};
  ASSERT_TRUE(p2.add_transaction(Transaction(addr2, addr1, Currency("RUB"), 221_c), true));
  ASSERT_TRUE(p2.add_transaction(Transaction(addr2, addr3, Currency("RUB"), 223_c), true));
  ASSERT_TRUE(p2.compose());

  Pool p3{p2.hash(), 2};
  ASSERT_TRUE(p3.add_transaction(Transaction(addr3, addr1, Currency("RUB"), 331_c), true));
  ASSERT_TRUE(p3.add_transaction(Transaction(addr3, addr1, Currency("RUB"), 333_c), true));
  ASSERT_TRUE(p3.compose());

  ASSERT_TRUE(s.pool_save(p1));
  ASSERT_TRUE(s.pool_save(p2));
  ASSERT_TRUE(s.pool_save(p3));

  EXPECT_EQ(s.get_last_by_source(addr3).amount(), 333_c);
  EXPECT_EQ(s.get_last_by_source(addr2).amount(), 223_c);
  EXPECT_EQ(s.get_last_by_source(addr1).amount(), 113_c);

  EXPECT_EQ(s.get_last_by_target(addr3).amount(), 223_c);
  EXPECT_EQ(s.get_last_by_target(addr2).amount(), 112_c);
  EXPECT_EQ(s.get_last_by_target(addr1).amount(), 333_c);

  ::csdb::Address addr4 = ::csdb::Address::from_string("0000000000000000000000000000000000000004");
  EXPECT_FALSE(s.get_last_by_source(addr4).is_valid());
  EXPECT_FALSE(s.get_last_by_target(addr4).is_valid());
}