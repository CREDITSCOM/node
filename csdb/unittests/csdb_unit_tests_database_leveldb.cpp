#include "csdb/database_leveldb.h"

#include <memory>
#include <algorithm>

#include <gtest/gtest.h>

#include "leveldb/db.h"
#include "leveldb/env.h"
#include "csdb/internal/utils.h"

class DatabaseLeveDBTest : public ::testing::Test
{
protected:
  void SetUp() override final
  {
    leveldb::Env *env = leveldb::Env::Default();
    EXPECT_TRUE(env->GetTestDirectory(&path_to_db_).ok());
    path_to_db_ += database_name_;

    if(env->FileExists(path_to_db_)) {
      EXPECT_TRUE(leveldb::DestroyDB(path_to_db_, leveldb::Options()).ok());
    }

    ::csdb::DatabaseLevelDB* db{new ::csdb::DatabaseLevelDB};
    ASSERT_TRUE(db->open(path_to_db_));
    db_.reset(db);
  }

  void TearDown() override final
  {
    db_.reset(nullptr);
    EXPECT_TRUE(leveldb::Env::Default()->FileExists(path_to_db_));
    EXPECT_TRUE(leveldb::DestroyDB(path_to_db_, leveldb::Options()).ok());
  }

protected:
  std::unique_ptr<::csdb::Database> db_;
  std::string path_to_db_;
  static constexpr const char* database_name_ = "/csdb_leveldb_unittests";
};

class DatabaseLeveDBTestNotOpen : public ::testing::Test
{
};

TEST_F(DatabaseLeveDBTestNotOpen, FailedCreation)
{
  std::unique_ptr<::csdb::DatabaseLevelDB> db{new ::csdb::DatabaseLevelDB};
  EXPECT_FALSE(db->open("/dev/null"));
  EXPECT_EQ(db->last_error(), ::csdb::Database::IOError);
}

TEST_F(DatabaseLeveDBTest, Create)
{
  EXPECT_TRUE(db_->is_open());
  EXPECT_EQ(db_->last_error(), ::csdb::Database::NoError);
}

TEST_F(DatabaseLeveDBTestNotOpen, FailedGetPut)
{
  std::unique_ptr<::csdb::Database> db{new ::csdb::DatabaseLevelDB};
  EXPECT_FALSE(db->put({1,1,1}, {2,2,2}));
  EXPECT_EQ(db->last_error(), ::csdb::Database::NotOpen);

  EXPECT_FALSE(db->get({1,1,1}));
  EXPECT_EQ(db->last_error(), ::csdb::Database::NotOpen);

  ::csdb::internal::byte_array result;
  EXPECT_FALSE(db->get({1,1,1}, &result));
  EXPECT_EQ(db->last_error(), ::csdb::Database::NotOpen);
}

TEST_F(DatabaseLeveDBTest, GetPut)
{
  EXPECT_TRUE(db_->is_open());
  EXPECT_TRUE(db_->put({1,1,1}, {2,2,2}));
  EXPECT_EQ(db_->last_error(), ::csdb::Database::NoError);
  EXPECT_TRUE(db_->put({2,2,2}, {3,3,3}));
  EXPECT_EQ(db_->last_error(), ::csdb::Database::NoError);
  EXPECT_TRUE(db_->put({4,4,4}, {5,5,5}));
  EXPECT_EQ(db_->last_error(), ::csdb::Database::NoError);
  EXPECT_TRUE(db_->put({0,0,0}, {0xFF,0xFF,0xFF}));
  EXPECT_EQ(db_->last_error(), ::csdb::Database::NoError);

  EXPECT_TRUE(db_->get({0,0,0}));
  EXPECT_EQ(db_->last_error(), ::csdb::Database::NoError);
  EXPECT_TRUE(db_->get({1,1,1}));
  EXPECT_EQ(db_->last_error(), ::csdb::Database::NoError);
  EXPECT_TRUE(db_->get({2,2,2}));
  EXPECT_EQ(db_->last_error(), ::csdb::Database::NoError);
  EXPECT_TRUE(db_->get({4,4,4}));
  EXPECT_EQ(db_->last_error(), ::csdb::Database::NoError);

  EXPECT_FALSE(db_->get({3,3,3}));
  EXPECT_EQ(db_->last_error(), ::csdb::Database::NotFound);

  ::csdb::internal::byte_array result;
  EXPECT_TRUE(db_->get({0,0,0}, &result));
  EXPECT_EQ(db_->last_error(), ::csdb::Database::NoError);
  EXPECT_EQ(result, ::csdb::internal::byte_array({0xFF,0xFF,0xFF}));
  EXPECT_TRUE(db_->get({1,1,1}, &result));
  EXPECT_EQ(db_->last_error(), ::csdb::Database::NoError);
  EXPECT_EQ(result, ::csdb::internal::byte_array({2,2,2}));
  EXPECT_TRUE(db_->get({2,2,2}, &result));
  EXPECT_EQ(db_->last_error(), ::csdb::Database::NoError);
  EXPECT_EQ(result, ::csdb::internal::byte_array({3,3,3}));
  EXPECT_TRUE(db_->get({4,4,4}, &result));
  EXPECT_EQ(db_->last_error(), ::csdb::Database::NoError);
  EXPECT_EQ(result, ::csdb::internal::byte_array({5,5,5}));

  EXPECT_FALSE(db_->get({3,3,3}, &result));
  EXPECT_EQ(db_->last_error(), ::csdb::Database::NotFound);
}

TEST_F(DatabaseLeveDBTestNotOpen, FailedRemove)
{
  std::unique_ptr<::csdb::Database> db{new ::csdb::DatabaseLevelDB};
  EXPECT_FALSE(db->remove({1,1,1}));
  EXPECT_EQ(db->last_error(), ::csdb::Database::NotOpen);
}

TEST_F(DatabaseLeveDBTest, Remove)
{
  EXPECT_TRUE(db_->is_open());
  EXPECT_TRUE(db_->put({1,1,1}, {2,2,2}));
  EXPECT_EQ(db_->last_error(), ::csdb::Database::NoError);
  EXPECT_TRUE(db_->put({2,2,2}, {3,3,3}));
  EXPECT_EQ(db_->last_error(), ::csdb::Database::NoError);
  EXPECT_TRUE(db_->put({4,4,4}, {5,5,5}));
  EXPECT_EQ(db_->last_error(), ::csdb::Database::NoError);
  EXPECT_TRUE(db_->put({0,0,0}, {0xFF,0xFF,0xFF}));

  EXPECT_TRUE(db_->remove({2,2,2}));
  EXPECT_EQ(db_->last_error(), ::csdb::Database::NoError);
  EXPECT_TRUE(db_->remove({3,3,3}));
  EXPECT_EQ(db_->last_error(), ::csdb::Database::NoError);
  EXPECT_TRUE(db_->remove({4,4,4}));
  EXPECT_EQ(db_->last_error(), ::csdb::Database::NoError);

  ::csdb::internal::byte_array result;
  EXPECT_TRUE(db_->get({0,0,0}, &result));
  EXPECT_EQ(db_->last_error(), ::csdb::Database::NoError);
  EXPECT_EQ(result, ::csdb::internal::byte_array({0xFF,0xFF,0xFF}));
  EXPECT_TRUE(db_->get({1,1,1}, &result));
  EXPECT_EQ(db_->last_error(), ::csdb::Database::NoError);
  EXPECT_EQ(result, ::csdb::internal::byte_array({2,2,2}));

  EXPECT_FALSE(db_->get({2,2,2}, &result));
  EXPECT_EQ(db_->last_error(), ::csdb::Database::NotFound);
  EXPECT_FALSE(db_->get({3,3,3}, &result));
  EXPECT_EQ(db_->last_error(), ::csdb::Database::NotFound);
  EXPECT_FALSE(db_->get({3,3,3}, &result));
  EXPECT_EQ(db_->last_error(), ::csdb::Database::NotFound);
}

TEST_F(DatabaseLeveDBTest, WriteBatch)
{
  EXPECT_TRUE(db_->is_open());
  EXPECT_TRUE(db_->write_batch({}));
  EXPECT_EQ(db_->last_error(), ::csdb::Database::NoError);
  EXPECT_TRUE(db_->write_batch({{{0,0,0}, {0xFF,0xFF,0xFF}}}));
  EXPECT_EQ(db_->last_error(), ::csdb::Database::NoError);
  EXPECT_TRUE(db_->write_batch({{{1,1,1}, {2,2,2}}, {{2,2,2}, {3,3,3}}, {{4,4,4}, {5,5,5}}}));
  EXPECT_EQ(db_->last_error(), ::csdb::Database::NoError);

  ::csdb::internal::byte_array result;
  EXPECT_TRUE(db_->get({0,0,0}, &result));
  EXPECT_EQ(db_->last_error(), ::csdb::Database::NoError);
  EXPECT_EQ(result, ::csdb::internal::byte_array({0xFF,0xFF,0xFF}));
  EXPECT_TRUE(db_->get({1,1,1}, &result));
  EXPECT_EQ(db_->last_error(), ::csdb::Database::NoError);
  EXPECT_EQ(result, ::csdb::internal::byte_array({2,2,2}));
  EXPECT_TRUE(db_->get({2,2,2}, &result));
  EXPECT_EQ(db_->last_error(), ::csdb::Database::NoError);
  EXPECT_EQ(result, ::csdb::internal::byte_array({3,3,3}));
  EXPECT_TRUE(db_->get({4,4,4}, &result));
  EXPECT_EQ(db_->last_error(), ::csdb::Database::NoError);
  EXPECT_EQ(result, ::csdb::internal::byte_array({5,5,5}));

  EXPECT_FALSE(db_->get({3,3,3}, &result));
  EXPECT_EQ(db_->last_error(), ::csdb::Database::NotFound);
}

TEST_F(DatabaseLeveDBTestNotOpen, FailedWriteBatch)
{
  std::unique_ptr<::csdb::Database> db{new ::csdb::DatabaseLevelDB};
  EXPECT_FALSE(db->write_batch({{{0,0,0}, {0xFF,0xFF,0xFF}}}));
  EXPECT_EQ(db->last_error(), ::csdb::Database::NotOpen);
}

TEST_F(DatabaseLeveDBTest, Iterator)
{
  EXPECT_TRUE(db_->is_open());
  ::csdb::Database::ItemList list{{{3,3,3}, {4,4,4}}, {{4,4,4}, {5,5,5}}, {{2,2,2}, {3,3,3}},
                                  {{0,0,0}, {1,1,1}}, {{1,1,1}, {2,2,2}}};
  EXPECT_TRUE(db_->write_batch(list));
  EXPECT_EQ(db_->last_error(), ::csdb::Database::NoError);

  auto db_it = db_->new_iterator();
  EXPECT_TRUE(db_it);
  EXPECT_FALSE(db_it->is_valid());

  std::sort(list.begin(), list.end());

  // Forward
  auto it = list.begin();
  db_it->seek_to_first();
  for (; it != list.end(); ++it, db_it->next()) {
    EXPECT_TRUE(db_it->is_valid());
    EXPECT_EQ(it->first, db_it->key());
    EXPECT_EQ(it->second, db_it->value());
  }

  // Reverse
  it = list.end() - 1;
  db_it->seek_to_last();
  while(true) {
    EXPECT_TRUE(db_it->is_valid());
    EXPECT_EQ(it->first, db_it->key());
    EXPECT_EQ(it->second, db_it->value());
    if (list.begin() == it) {
      break;
    }
    --it;
    db_it->prev();
  };

  // Forward from center
  it = list.begin() + (list.size() / 2);
  db_it->seek(it->first);
  for (; it != list.end(); ++it, db_it->next()) {
    EXPECT_TRUE(db_it->is_valid());
    EXPECT_EQ(it->first, db_it->key());
    EXPECT_EQ(it->second, db_it->value());
  }

  // Reverse from center
  it = list.begin() + (list.size() / 2);
  db_it->seek(it->first);
  while(true) {
    EXPECT_TRUE(db_it->is_valid());
    EXPECT_EQ(it->first, db_it->key());
    EXPECT_EQ(it->second, db_it->value());
    if (list.begin() == it) {
      break;
    }
    --it;
    db_it->prev();
  };
}

TEST_F(DatabaseLeveDBTestNotOpen, FailedIterator)
{
  std::unique_ptr<::csdb::Database> db{new ::csdb::DatabaseLevelDB};
  EXPECT_FALSE(db->new_iterator());
  EXPECT_EQ(db->last_error(), ::csdb::Database::NotOpen);
}
