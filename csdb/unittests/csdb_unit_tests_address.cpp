#include "csdb/address.h"

#include <gtest/gtest.h>

#include "priv_crypto.h"

class AddressTest : public ::testing::Test
{
};

::std::ostream& operator <<(::std::ostream& os, const ::csdb::Address& value)
{
  os << value.to_string();
  return os;
}

using namespace csdb;

TEST_F(AddressTest, EmptyAddress)
{
  Address a;
  EXPECT_FALSE(a.is_valid());
  EXPECT_TRUE(a.to_string().empty());
}

TEST_F(AddressTest, FromValidString)
{
  EXPECT_TRUE(Address::from_string("0000000000000000000000000000000000000000").is_valid());
  Address a1 = Address::from_string("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
  Address a2 = Address::from_string("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
  Address a3 = Address::from_string("aAaAaAaAaAaAaAaAaAaAaAaAaAaAaAaAaAaAaAaa");
  EXPECT_TRUE(a1.is_valid());
  EXPECT_TRUE(a2.is_valid());
  EXPECT_TRUE(a3.is_valid());
  EXPECT_EQ(a1, a2);
  EXPECT_EQ(a1, a3);
  EXPECT_EQ(a2, a3);
}

TEST_F(AddressTest, FromInvalidString)
{
  EXPECT_FALSE(Address::from_string("00000000000000000000000000000000000000").is_valid());
  EXPECT_FALSE(Address::from_string("000000000000000000000000000000000000000000").is_valid());
  EXPECT_FALSE(Address::from_string("000000000000000000000000000000000000000z").is_valid());
  EXPECT_FALSE(Address::from_string("z000000000000000000000000000000000000000").is_valid());
}

TEST_F(AddressTest, FromValidPublicKey)
{
  ::csdb::internal::byte_array key1(::csdb::priv::crypto::public_key_size);
  ::csdb::internal::byte_array key2(::csdb::priv::crypto::public_key_size);

  for (size_t i = 0; i < ::csdb::priv::crypto::public_key_size; ++i) {
    key1[i] = 0;
    key2[i] = static_cast<::csdb::internal::byte_array::value_type>(i);
  }

  Address a1 = Address::from_public_key(key1);
  Address a2 = Address::from_public_key(key1);
  Address a3 = Address::from_public_key(key2);
  Address a4 = Address::from_public_key(key2);
  EXPECT_TRUE(a1.is_valid());
  EXPECT_TRUE(a2.is_valid());
  EXPECT_TRUE(a3.is_valid());
  EXPECT_TRUE(a4.is_valid());
  EXPECT_EQ(a1, a2);
  EXPECT_EQ(a3, a4);
  EXPECT_NE(a1, a3);
  EXPECT_NE(a2, a4);
  EXPECT_EQ(a1.public_key(), key1);
  EXPECT_EQ(a2.public_key(), key1);
  EXPECT_EQ(a3.public_key(), key2);
  EXPECT_EQ(a4.public_key(), key2);
}

TEST_F(AddressTest, FromInvalidPublicKey)
{
  EXPECT_FALSE(Address::from_public_key({1,2,3}).is_valid());
  EXPECT_FALSE(Address::from_public_key(
                 ::csdb::internal::byte_array(::csdb::priv::crypto::public_key_size - 1)).is_valid());
  EXPECT_FALSE(Address::from_public_key(
                 ::csdb::internal::byte_array(::csdb::priv::crypto::public_key_size + 1)).is_valid());
}
