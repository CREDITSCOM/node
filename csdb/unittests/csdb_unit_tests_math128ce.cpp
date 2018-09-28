#include "csdb/internal/math128ce.h"

#include <gtest/gtest.h>

using namespace csdb::internal;

class Math128CETest : public ::testing::Test
{
};

TEST_F(Math128CETest, uint128_t_constructors)
{
  constexpr uint128_t n1;
  EXPECT_EQ(n1.lo_, 0ULL);
  EXPECT_EQ(n1.hi_, 0ULL);

  constexpr uint128_t n2(1);
  EXPECT_EQ(n2.lo_, 1ULL);
  EXPECT_EQ(n2.hi_, 0ULL);

  constexpr uint128_t n3(1,1);
  EXPECT_EQ(n3.lo_, 1ULL);
  EXPECT_EQ(n3.hi_, 1ULL);
}

TEST_F(Math128CETest, uint128_t_addition64)
{
  constexpr uint128_t n1 = uint128_t() + 1;
  EXPECT_EQ(n1.lo_, 1ULL);
  EXPECT_EQ(n1.hi_, 0ULL);

  constexpr uint128_t n2 = uint128_t(1) + 1;
  EXPECT_EQ(n2.lo_, 2ULL);
  EXPECT_EQ(n2.hi_, 0ULL);

  constexpr uint128_t n3 = uint128_t(1,10) + 10;
  EXPECT_EQ(n3.lo_, 11ULL);
  EXPECT_EQ(n3.hi_, 10ULL);

  constexpr uint128_t n4 = uint128_t(0xFFFFFFFFFFFFFFFFULL, 2) + 1;
  EXPECT_EQ(n4.lo_, 0ULL);
  EXPECT_EQ(n4.hi_, 3ULL);

  constexpr uint128_t n5 = uint128_t(10, 5) + 0xFFFFFFFFFFFFFFFFULL;
  EXPECT_EQ(n5.lo_, 9ULL);
  EXPECT_EQ(n5.hi_, 6ULL);

  constexpr uint128_t n6 = uint128_t(0xF, 5) + 0xFFFFFFFFFFFFFFF0ULL;
  EXPECT_EQ(n6.lo_, 0xFFFFFFFFFFFFFFFFULL);
  EXPECT_EQ(n6.hi_, 5ULL);

  constexpr uint128_t n7 = uint128_t(0x11, 5) + 0xFFFFFFFFFFFFFFF0ULL;
  EXPECT_EQ(n7.lo_, 1ULL);
  EXPECT_EQ(n7.hi_, 6ULL);
}

TEST_F(Math128CETest, uint128_t_addition128)
{
  constexpr uint128_t n1 = uint128_t() + uint128_t(1,1);
  EXPECT_EQ(n1.lo_, 1ULL);
  EXPECT_EQ(n1.hi_, 1ULL);

  constexpr uint128_t n2 = uint128_t(1) + uint128_t(1,2);
  EXPECT_EQ(n2.lo_, 2ULL);
  EXPECT_EQ(n2.hi_, 2ULL);

  constexpr uint128_t n3 = uint128_t(1,10) + uint128_t(10,3);
  EXPECT_EQ(n3.lo_, 11ULL);
  EXPECT_EQ(n3.hi_, 13ULL);

  constexpr uint128_t n4 = uint128_t(0xFFFFFFFFFFFFFFFFULL, 2) + uint128_t(1, 4);
  EXPECT_EQ(n4.lo_, 0ULL);
  EXPECT_EQ(n4.hi_, 7ULL);

  constexpr uint128_t n5 = uint128_t(10, 5) + uint128_t(0xFFFFFFFFFFFFFFFFULL, 5);
  EXPECT_EQ(n5.lo_, 9ULL);
  EXPECT_EQ(n5.hi_, 11ULL);

  constexpr uint128_t n6 = uint128_t(0xF, 5) + uint128_t(0xFFFFFFFFFFFFFFF0ULL, 6);
  EXPECT_EQ(n6.lo_, 0xFFFFFFFFFFFFFFFFULL);
  EXPECT_EQ(n6.hi_, 11ULL);

  constexpr uint128_t n7 = uint128_t(0x11, 5) + uint128_t(0xFFFFFFFFFFFFFFF0ULL, 7);
  EXPECT_EQ(n7.lo_, 1ULL);
  EXPECT_EQ(n7.hi_, 13ULL);
}

TEST_F(Math128CETest, uint128_t_multiplication)
{
  constexpr uint128_t n1 = uint128_t::mul(1, 1);
  EXPECT_EQ(n1.lo_, 1ULL);
  EXPECT_EQ(n1.hi_, 0ULL);

  constexpr uint128_t n2 = uint128_t::mul(0x100000000ULL, 0x100000000ULL);
  EXPECT_EQ(n2.lo_, 0ULL);
  EXPECT_EQ(n2.hi_, 1ULL);

  constexpr uint128_t n3 = uint128_t::mul(0x1000000000ULL, 0x1000000000ULL);
  EXPECT_EQ(n3.lo_, 0ULL);
  EXPECT_EQ(n3.hi_, 0x100ULL);

  constexpr uint128_t n4 = uint128_t::mul(0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL);
  EXPECT_EQ(n4.lo_, 1ULL);
  EXPECT_EQ(n4.hi_, 0xFFFFFFFFFFFFFFFEULL);

  constexpr uint128_t n5 = uint128_t::mul(17446744073709551615ULL, 7446744073709551615ULL);
  EXPECT_EQ(n5.lo_, 6541590728541208577ULL);
  EXPECT_EQ(n5.hi_, 7043055268576579053ULL);

  constexpr uint128_t n6 = uint128_t::mul(12446744073709551615ULL, 10438186666892946874ULL);
  EXPECT_EQ(9852383995793206715ULL - (0xFFFFFFFFFFFFFFFF - n6.lo_ + 1), 6541590728541208577ULL );
  EXPECT_EQ(n6.hi_ + 1, 7043055268576579053ULL);
}

TEST_F(Math128CETest, uint128_t_division64)
{
  constexpr auto n1 = uint128_t(1, 1).div(1);
  EXPECT_EQ(n1.quotient_.lo_, 1ULL);
  EXPECT_EQ(n1.quotient_.hi_, 1ULL);
  EXPECT_EQ(n1.remainder_, 0ULL);

  constexpr auto n2 = uint128_t(3, 0).div(2);
  EXPECT_EQ(n2.quotient_.lo_, 1ULL);
  EXPECT_EQ(n2.quotient_.hi_, 0ULL);
  EXPECT_EQ(n2.remainder_, 1ULL);

  constexpr auto n3 = uint128_t(4, 0).div(2);
  EXPECT_EQ(n3.quotient_.lo_, 2ULL);
  EXPECT_EQ(n3.quotient_.hi_, 0ULL);
  EXPECT_EQ(n3.remainder_, 0ULL);

  constexpr auto n4 = uint128_t(1, 1).div(2);
  EXPECT_EQ(n4.quotient_.lo_, 0x8000000000000000ULL);
  EXPECT_EQ(n4.quotient_.hi_, 0ULL);
  EXPECT_EQ(n4.remainder_, 1ULL);

  constexpr auto n5 = uint128_t(8, 0).div(7);
  EXPECT_EQ(n5.quotient_.lo_, 1);
  EXPECT_EQ(n5.quotient_.hi_, 0ULL);
  EXPECT_EQ(n5.remainder_, 1);

  constexpr auto n6 = uint128_t((123456789ULL * 987654321ULL) + 10ULL, 0).div(123456789);
  EXPECT_EQ(n6.quotient_.lo_, 987654321);
  EXPECT_EQ(n6.quotient_.hi_, 0ULL);
  EXPECT_EQ(n6.remainder_, 10ULL);

  constexpr auto n7 = uint128_t((123456789ULL * 987654321ULL) + 10ULL, 0).div(987654321);
  EXPECT_EQ(n7.quotient_.lo_, 123456789);
  EXPECT_EQ(n7.quotient_.hi_, 0ULL);
  EXPECT_EQ(n7.remainder_, 10ULL);

  constexpr auto n8 = uint128_t(0, 0xFFFFFFFFULL).div(0xFFFFFFFFFFFFFFFFULL);
  EXPECT_EQ(n8.quotient_.lo_, 0xFFFFFFFFULL);
  EXPECT_EQ(n8.quotient_.hi_, 0ULL);
  EXPECT_EQ(n8.remainder_, 0xFFFFFFFFULL);

  constexpr auto n9 = uint128_t::mul(17446744073709551615ULL, 7446744073709551615ULL).div(12446744073709551615ULL);
  EXPECT_EQ(n9.quotient_.lo_, 10438186666892946874ULL);
  EXPECT_EQ(n9.quotient_.hi_, 0ULL);
  EXPECT_EQ(n9.remainder_, 9852383995793206715ULL);

  constexpr auto n10 = uint128_t::mul(1, 1000000000000000000ULL).div(2);
  EXPECT_EQ(n10.quotient_.lo_, 500000000000000000ULL);
  EXPECT_EQ(n10.quotient_.hi_, 0ULL);

  constexpr auto n11 = uint128_t::mul(1, 1000000000000000000ULL).div(3);
  EXPECT_EQ(n11.quotient_.lo_, 333333333333333333ULL);
  EXPECT_EQ(n11.quotient_.hi_, 0ULL);

  constexpr auto n12 = (uint128_t::mul(2, 1000000000000000000ULL) + 1).div(3);
  EXPECT_EQ(n12.quotient_.lo_, 666666666666666667ULL);
  EXPECT_EQ(n12.quotient_.hi_, 0ULL);

  constexpr auto n13 = (uint128_t::mul(3, 1000000000000000000ULL) + (700 / 2)).div(700);
  EXPECT_EQ(n13.quotient_.lo_, 4285714285714286ULL);
  EXPECT_EQ(n13.quotient_.hi_, 0ULL);
}
