#include "csdb/database.h"

#include <gtest/gtest.h>

#include "csdb/internal/utils.h"

class DatabaseTest : public ::testing::Test
{
protected:
  class DatabaseImpl : public ::csdb::Database
  {
  private:
    bool is_open() const override final {return false;}
    bool put(const byte_array&, const byte_array&) override final {return false;}
    bool get(const byte_array&, byte_array*) override final {return false;}
    bool remove(const byte_array&) override final {return false;}
    bool write_batch(const ItemList&) override final {return false;}
    IteratorPtr new_iterator() override final {return IteratorPtr{nullptr};}

  public:
    using ::csdb::Database::set_last_error;
  };
};

TEST_F(DatabaseTest, LastError)
{
  DatabaseImpl db;
  EXPECT_EQ(db.last_error(), ::csdb::Database::NoError);
  EXPECT_FALSE(db.last_error_message().empty());

  db.set_last_error(::csdb::Database::NotFound);
  EXPECT_EQ(db.last_error(), ::csdb::Database::NotFound);
  EXPECT_FALSE(db.last_error_message().empty());

  db.set_last_error(::csdb::Database::Corruption, nullptr);
  EXPECT_EQ(db.last_error(), ::csdb::Database::Corruption);
  EXPECT_FALSE(db.last_error_message().empty());

  db.set_last_error(::csdb::Database::NotSupported, std::string("Test"));
  EXPECT_EQ(db.last_error(), ::csdb::Database::NotSupported);
  EXPECT_EQ(db.last_error_message(), "Test");

  db.set_last_error(::csdb::Database::InvalidArgument, "Test");
  EXPECT_EQ(db.last_error(), ::csdb::Database::InvalidArgument);
  EXPECT_EQ(db.last_error_message(), "Test");

  db.set_last_error(::csdb::Database::IOError, "%s%04u", "Test", 100);
  EXPECT_EQ(db.last_error(), ::csdb::Database::IOError);
  EXPECT_EQ(db.last_error_message(), "Test0100");

  db.set_last_error();
  EXPECT_EQ(db.last_error(), ::csdb::Database::NoError);
  EXPECT_FALSE(db.last_error_message().empty());
}
