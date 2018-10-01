#include "csdb/user_field.h"

#include <iostream>

#include "csdb_unit_tests_environment.h"

#include "csdb/internal/types.h"
#include "binary_streams.h"

class UserFieldTest : public ::testing::Test
{
protected:
  ::csdb::internal::byte_array encode(::csdb::UserField source)
  {
    ::csdb::priv::obstream os;
    os.put(source);
    return os.buffer();
  }

  bool decode(::csdb::UserField& result, const ::csdb::internal::byte_array& source)
  {
    ::csdb::priv::ibstream is(source.data(), source.size());
    return is.get(result) && is.empty();
  }
};

class UserFieldSerializationTest : public UserFieldTest,
                                   public ::testing::WithParamInterface<::csdb::UserField>
{
};

using namespace ::csdb;

TEST_F(UserFieldTest, Construction)
{
  {
    UserField f;
    EXPECT_FALSE(f.is_valid());
    EXPECT_EQ(f.type(), UserField::Unknown);
    EXPECT_EQ(f.value<int>(), 0);
    EXPECT_TRUE(f.value<::std::string>().empty());
    EXPECT_EQ(f.value<Amount>(), 0_c);
  }

  {
    UserField f(true);
    EXPECT_TRUE(f.is_valid());
    EXPECT_EQ(f.type(), UserField::Integer);
    EXPECT_NE(f.value<int>(), 0);
    EXPECT_TRUE(f.value<bool>());
    EXPECT_TRUE(f.value<::std::string>().empty());
    EXPECT_EQ(f.value<Amount>(), 0_c);
  }

  {
    UserField f('A');
    EXPECT_TRUE(f.is_valid());
    EXPECT_EQ(f.type(), UserField::Integer);
    EXPECT_EQ(f.value<char>(), 'A');
    EXPECT_TRUE(f.value<::std::string>().empty());
    EXPECT_EQ(f.value<Amount>(), 0_c);
  }

  {
    UserField f(-1);
    EXPECT_TRUE(f.is_valid());
    EXPECT_EQ(f.type(), UserField::Integer);
    EXPECT_EQ(f.value<int>(), -1);
    EXPECT_TRUE(f.value<::std::string>().empty());
    EXPECT_EQ(f.value<Amount>(), 0_c);
  }

  {
    UserField f(std::string("Test string"));
    EXPECT_TRUE(f.is_valid());
    EXPECT_EQ(f.type(), UserField::String);
    EXPECT_EQ(f.value<int>(), 0);
    EXPECT_EQ(f.value<::std::string>(), "Test string");
    EXPECT_EQ(f.value<Amount>(), 0_c);
  }

  {
    UserField f("Test string");
    EXPECT_TRUE(f.is_valid());
    EXPECT_EQ(f.type(), UserField::String);
    EXPECT_EQ(f.value<int>(), 0);
    EXPECT_EQ(f.value<::std::string>(), "Test string");
    EXPECT_EQ(f.value<Amount>(), 0_c);
  }

  {
    UserField f(-123.456_c);
    EXPECT_TRUE(f.is_valid());
    EXPECT_EQ(f.type(), UserField::Amount);
    EXPECT_EQ(f.value<int>(), 0);
    EXPECT_TRUE(f.value<::std::string>().empty());
    EXPECT_EQ(f.value<Amount>(), -123.456_c);
  }
}

TEST_F(UserFieldTest, SerializeInvalid)
{
  UserField f;
  EXPECT_FALSE(f.is_valid());
  EXPECT_TRUE(encode(f).empty());
}

TEST_P(UserFieldSerializationTest, Valid)
{
  UserField src = GetParam();
  EXPECT_TRUE(src.is_valid());

  ::csdb::internal::byte_array enc = encode(src);
  EXPECT_FALSE(enc.empty());

  UserField res;
  EXPECT_FALSE(res.is_valid());
  EXPECT_TRUE(decode(res, enc));
  EXPECT_EQ(src, res);
}

TEST_P(UserFieldSerializationTest, Truncate)
{
  UserField src = GetParam();
  EXPECT_TRUE(src.is_valid());

  ::csdb::internal::byte_array valid = encode(src);
  EXPECT_FALSE(valid.empty());

  ::csdb::internal::byte_array invalid(valid);
  invalid.resize(invalid.size() - 1);

  {
    UserField res(1);
    EXPECT_FALSE(decode(res, invalid));
    EXPECT_EQ(res, UserField(1));
  }

  {
    UserField res("Test string");
    EXPECT_FALSE(decode(res, invalid));
    EXPECT_EQ(res, UserField("Test string"));
  }

  {
    UserField res(123.456_c);
    EXPECT_FALSE(decode(res, invalid));
    EXPECT_EQ(res, UserField(123.456_c));
  }
}

TEST_P(UserFieldSerializationTest, Corrupt)
{
  UserField src = GetParam();
  EXPECT_TRUE(src.is_valid());

  ::csdb::internal::byte_array valid = encode(src);
  EXPECT_FALSE(valid.empty());

  ::csdb::internal::byte_array invalid(valid);
  invalid[0] = ~invalid[0];

  {
    UserField res(1);
    EXPECT_FALSE(decode(res, invalid));
    EXPECT_EQ(res, UserField(1));
  }

  {
    UserField res("Test string");
    EXPECT_FALSE(decode(res, invalid));
    EXPECT_EQ(res, UserField("Test string"));
  }

  {
    UserField res(123.456_c);
    EXPECT_FALSE(decode(res, invalid));
    EXPECT_EQ(res, UserField(123.456_c));
  }
}

INSTANTIATE_TEST_CASE_P(, UserFieldSerializationTest, ::testing::Values(
  UserField(true),
  UserField(false),
  UserField(0),
  UserField(-1),
  UserField(1),
  UserField("Test string"),
  UserField(123.456_c)
));
