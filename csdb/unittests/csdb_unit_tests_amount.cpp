#include "csdb/amount.h"
#include "binary_streams.h"
#include <gtest/gtest.h>

class AmountTest : public ::testing::Test
{
};

using namespace csdb;

TEST_F(AmountTest, SimpleConstructors)
{
  constexpr Amount a;
  EXPECT_EQ(a.integral(), 0);
  EXPECT_EQ(a.fraction(), 0);
  EXPECT_EQ(a.round(), 0);
  EXPECT_EQ(a.to_double(), 0.0);

  constexpr Amount b(1);
  EXPECT_EQ(b.integral(), 1);
  EXPECT_EQ(b.fraction(), 0);
  EXPECT_EQ(b.round(), 1);
  EXPECT_EQ(b.to_double(), 1.0);

  constexpr Amount c(-1);
  EXPECT_EQ(c.integral(), -1);
  EXPECT_EQ(c.fraction(), 0);
  EXPECT_EQ(c.round(), -1);
  EXPECT_EQ(c.to_double(), -1.0);
}

TEST_F(AmountTest, FractionConstructors)
{
  EXPECT_THROW(Amount(1, 1, 1), std::invalid_argument);
  EXPECT_THROW(Amount(1, 5, 2), std::invalid_argument);
  EXPECT_THROW(Amount(1, 5, 5), std::invalid_argument);
  EXPECT_THROW(Amount(1, 100, 5), std::invalid_argument);
  EXPECT_THROW(Amount(1, 101, 5), std::invalid_argument);

  constexpr Amount a1(1, 2);
  EXPECT_EQ(a1.integral(), 1);
  EXPECT_EQ(a1.fraction(), 20000000000000000ULL);
  EXPECT_EQ(a1.round(), 1);
  EXPECT_EQ(a1.to_double(), 1.02);

  constexpr Amount a2(1, 5, 1000);
  EXPECT_EQ(a2.integral(), 1);
  EXPECT_EQ(a2.fraction(), 5000000000000000ULL);
  EXPECT_EQ(a2.round(), 1);
  EXPECT_EQ(a2.to_double(), 1.005);

  constexpr Amount a3(-1, 4, 10);
  EXPECT_EQ(a3.integral(), -1);
  EXPECT_EQ(a3.fraction(), 400000000000000000ULL);
  EXPECT_EQ(a3.round(), -1);
  EXPECT_EQ(a3.to_double(), -0.6);

  constexpr Amount a4(-1, 5, 10);
  EXPECT_EQ(a4.integral(), -1);
  EXPECT_EQ(a4.fraction(), 500000000000000000ULL);
  EXPECT_EQ(a4.round(), 0);
  EXPECT_EQ(a4.to_double(), -0.5);

  constexpr Amount a5(0, 123456789, 1000000000);
  EXPECT_EQ(a5.integral(), 0);
  EXPECT_EQ(a5.fraction(), 123456789000000000ULL);
  EXPECT_EQ(a5.round(), 0);
  EXPECT_EQ(a5.to_double(), 0.123456789);
}

TEST_F(AmountTest, Serialization)
{
  ::csdb::priv::obstream o;
  o.put(Amount(5));
  o.put(Amount(-5));
  o.put(Amount(5, 20));
  o.put(Amount(-5, 20));
  o.put(Amount(5, 40, 10000));
  o.put(Amount(-5, 40, 10000));

  ::csdb::priv::ibstream i(o.buffer());
  Amount a;
  EXPECT_TRUE(i.get(a));
  EXPECT_EQ(a, Amount(5));
  EXPECT_TRUE(i.get(a));
  EXPECT_EQ(a, Amount(-5));
  EXPECT_TRUE(i.get(a));
  EXPECT_EQ(a, Amount(5, 20));
  EXPECT_TRUE(i.get(a));
  EXPECT_EQ(a, Amount(-5, 20));
  EXPECT_TRUE(i.get(a));
  EXPECT_EQ(a, Amount(5, 40, 10000));
  EXPECT_TRUE(i.get(a));
  EXPECT_EQ(a, Amount(-5, 40, 10000));
}

TEST_F(AmountTest, LiteralOperator)
{
  EXPECT_EQ(Amount(5), 5_c);
  EXPECT_EQ(Amount(5, 2), 5.02_c);
  EXPECT_EQ(Amount(5, 20), 5.20_c);
  EXPECT_EQ(Amount(5, 2, 1000), 5.002_c);
  EXPECT_EQ(Amount(55, 2, 1000), 55.002_c);
  EXPECT_EQ(Amount(0, 123456789, 1000000000), 0.123456789_c);
}

TEST_F(AmountTest, Comparison)
{
  EXPECT_TRUE(1_c < 2_c);
  EXPECT_FALSE(2_c < 1_c);
  EXPECT_TRUE(1.1_c < 1.2_c);
  EXPECT_FALSE(1.2_c < 1.1_c);
  EXPECT_TRUE(1.01_c < 1.1_c);
  EXPECT_FALSE(1.1_c < 1.01_c);
  EXPECT_FALSE(5_c < 5_c);

  EXPECT_TRUE(2_c > 1_c);
  EXPECT_FALSE(1_c > 2_c);
  EXPECT_TRUE(1.2_c > 1.1_c);
  EXPECT_FALSE(1.1_c > 1.2_c);
  EXPECT_TRUE(1.1_c > 1.01_c);
  EXPECT_FALSE(1.01_c > 1.1_c);
  EXPECT_FALSE(5_c > 5_c);

  EXPECT_TRUE(1_c <= 2_c);
  EXPECT_FALSE(2_c <= 1_c);
  EXPECT_TRUE(1.1_c <= 1.2_c);
  EXPECT_FALSE(1.2_c <= 1.1_c);
  EXPECT_TRUE(1.01_c <= 1.1_c);
  EXPECT_FALSE(1.1_c <= 1.01_c);
  EXPECT_TRUE(5_c <= 5_c);

  EXPECT_TRUE(2_c >= 1_c);
  EXPECT_FALSE(1_c >= 2_c);
  EXPECT_TRUE(1.2_c >= 1.1_c);
  EXPECT_FALSE(1.1_c >= 1.2_c);
  EXPECT_TRUE(1.1_c >= 1.01_c);
  EXPECT_FALSE(1.01_c >= 1.1_c);
  EXPECT_TRUE(5_c >= 5_c);
}

TEST_F(AmountTest, UnaryMinus)
{
  {
    constexpr const Amount a = -0_c;
    EXPECT_EQ(a.integral(), 0);
    EXPECT_EQ(a.fraction(), 0ULL);
  }

  {
    constexpr const Amount a = -1_c;
    EXPECT_EQ(a.integral(), -1);
    EXPECT_EQ(a.fraction(), 0ULL);
    constexpr const Amount b = -a;
    EXPECT_EQ(b.integral(), 1);
    EXPECT_EQ(b.fraction(), 0ULL);
  }

  {
    constexpr const Amount a = -0.4_c;
    EXPECT_EQ(a.integral(), -1);
    EXPECT_EQ(a.fraction(), 600000000000000000ULL);
    constexpr const Amount b = -a;
    EXPECT_EQ(b.integral(), 0);
    EXPECT_EQ(b.fraction(), 400000000000000000ULL);
  }

  {
    constexpr const Amount a = -2.05_c;
    EXPECT_EQ(a.integral(), -3);
    EXPECT_EQ(a.fraction(), 950000000000000000ULL);
    constexpr const Amount b = -a;
    EXPECT_EQ(b.integral(), 2);
    EXPECT_EQ(b.fraction(), 50000000000000000ULL);
  }
}

TEST_F(AmountTest, CounstructFromDouble)
{
  {
    const Amount a{1.0};
    EXPECT_EQ(a.integral(), 1);
    EXPECT_EQ(a.fraction(), 0ULL);
  }

  {
    const Amount a{-1.0};
    EXPECT_EQ(a.integral(), -1);
    EXPECT_EQ(a.fraction(), 0ULL);
  }

  {
    const Amount a{1.1};
    EXPECT_EQ(a.integral(), 1);
    EXPECT_EQ(a.fraction(), 100000000000000000ULL);
  }

  {
    const Amount a{-1.1};
    EXPECT_EQ(a.integral(), -2);
    EXPECT_EQ(a.fraction(), 900000000000000000ULL);
  }

  EXPECT_THROW(Amount(static_cast<double>(std::numeric_limits<int32_t>::max()) + 1), std::overflow_error);
  EXPECT_THROW(Amount(static_cast<double>(std::numeric_limits<int32_t>::min()) - 1), std::overflow_error);
}

TEST_F(AmountTest, AssignmentPlus)
{
  {
    Amount a = 1_c;
    a += 1_c;
    EXPECT_EQ(a, 2_c);
  }

  {
    Amount a = 1.1_c;
    a += 1.1_c;
    EXPECT_EQ(a, 2.2_c);
  }

  {
    Amount a = 1.6_c;
    a += 1.7_c;
    EXPECT_EQ(a, 3.3_c);
  }

  {
    Amount a = 1.6_c;
    a += -1.7_c;
    EXPECT_EQ(a, -0.1_c);
  }

  {
    Amount a = 1.1_c;
    a += 10;
    EXPECT_EQ(a, 11.1_c);
  }

  {
    Amount a = 1.1_c;
    a += (-10);
    EXPECT_EQ(a, -8.9_c);
  }

  {
    Amount a = 1.1_c;
    a += 1.1;
    EXPECT_EQ(a, 2.2_c);
  }

  {
    Amount a = 1.6_c;
    a += 1.7;
    EXPECT_EQ(a, 3.3_c);
  }

  {
    Amount a = 1.6_c;
    a += -1.7;
    EXPECT_EQ(a, -0.1_c);
  }
}

TEST_F(AmountTest, AssignmentMinus)
{
  {
    Amount a = 2_c;
    a -= 1_c;
    EXPECT_EQ(a, 1_c);
  }

  {
    Amount a = 2.2_c;
    a -= 1.1_c;
    EXPECT_EQ(a, 1.1_c);
  }

  {
    Amount a = 1.6_c;
    a -= 1.7_c;
    EXPECT_EQ(a, -0.1_c);
  }

  {
    Amount a = 1.6_c;
    a -= -1.7_c;
    EXPECT_EQ(a, 3.3_c);
  }

  {
    Amount a = 11.1_c;
    a -= 10;
    EXPECT_EQ(a, 1.1_c);
  }

  {
    Amount a = 1.1_c;
    a -= (-10);
    EXPECT_EQ(a, 11.1_c);
  }

  {
    Amount a = 2.2_c;
    a -= 1.1;
    EXPECT_EQ(a, 1.1_c);
  }

  {
    Amount a = 1.6_c;
    a -= 1.7;
    EXPECT_EQ(a, -0.1_c);
  }

  {
    Amount a = 1.6_c;
    a -= -1.7;
    EXPECT_EQ(a, 3.3_c);
  }
}

TEST_F(AmountTest, Plus)
{
  EXPECT_EQ(1_c + 1_c, 2_c);
  EXPECT_EQ(1.1_c + 1.1_c, 2.2_c);
  EXPECT_EQ(1.5_c + 2.5_c, 4_c);
  EXPECT_EQ(1.6_c + 2.6_c, 4.2_c);
  EXPECT_EQ(2.6_c + (-1.6_c), 1_c);

  EXPECT_EQ(1_c + 1, 2_c);
  EXPECT_EQ(2_c + (-1), 1_c);
  EXPECT_EQ((-1_c) + 2, 1_c);
  EXPECT_EQ((-0.5_c) + 1, 0.5_c);

  EXPECT_EQ(1_c + 1.0, 2_c);
  EXPECT_EQ(1.1_c + 1.1, 2.2_c);
  EXPECT_EQ(1.5_c + 2.5, 4_c);
  EXPECT_EQ(1.6_c + 2.6, 4.2_c);
  EXPECT_EQ(2.6_c + (-1.6), 1_c);

  EXPECT_EQ(1 + 1_c , 2_c);
  EXPECT_EQ((-1) + 2_c, 1_c);
  EXPECT_EQ(2 + (-1_c), 1_c);
  EXPECT_EQ(1 + (-0.5_c), 0.5_c);

  EXPECT_EQ(1.0 + 1_c, 2_c);
  EXPECT_EQ(1.1 + 1.1_c, 2.2_c);
  EXPECT_EQ(2.5 + 1.5_c, 4_c);
  EXPECT_EQ(2.6 + 1.6_c, 4.2_c);
  EXPECT_EQ((-1.6) + 2.6_c, 1_c);
}

TEST_F(AmountTest, Minus)
{
  EXPECT_EQ(2_c - 1_c, 1_c);
  EXPECT_EQ(2.2_c - 1.1_c, 1.1_c);
  EXPECT_EQ(1.5_c - 2.5_c, -1_c);
  EXPECT_EQ(1.3_c - 2.6_c, -1.3_c);
  EXPECT_EQ(2.6_c - (-1.6_c), 4.2_c);

  EXPECT_EQ(2_c - 1, 1_c);
  EXPECT_EQ(1_c - (-1), 2_c);
  EXPECT_EQ(1_c - 2, -1_c);
  EXPECT_EQ((-1_c) - 2, -3_c);
  EXPECT_EQ(0.5_c - 1, -0.5_c);
  EXPECT_EQ(0.2_c - 1, -0.8_c);

  EXPECT_EQ(2_c - 1.0, 1_c);
  EXPECT_EQ(2.2_c - 1.1, 1.1_c);
  EXPECT_EQ(1.5_c - 2.5, -1_c);
  EXPECT_EQ(1.3_c - 2.6, -1.3_c);
  EXPECT_EQ(2.6_c - (-1.6), 4.2_c);

  EXPECT_EQ(2 - 1_c, 1_c);
  EXPECT_EQ(1 - (-1_c), 2_c);
  EXPECT_EQ(1 - 2_c, -1_c);
  EXPECT_EQ((-1) - 2_c, -3_c);
  EXPECT_EQ(1 - 1.5_c, -0.5_c);
  EXPECT_EQ(1 - 1.2_c, -0.2_c);

  EXPECT_EQ(2.0 - 1.0_c, 1_c);
  EXPECT_EQ(2.2 - 1.1_c, 1.1_c);
  EXPECT_EQ(1.5 - 2.5_c, -1_c);
  EXPECT_EQ(1.3 - 2.6_c, -1.3_c);
  EXPECT_EQ(2.6 - (-1.6_c), 4.2_c);
}

TEST_F(AmountTest, Multiply)
{
  EXPECT_EQ(1_c * 1, 1_c);
  EXPECT_EQ(2_c * 2, 4_c);
  EXPECT_EQ((-2_c) * 2, -4_c);
  EXPECT_EQ(2_c * (-2), -4_c);
  EXPECT_EQ(-2_c * (-2), 4_c);
  EXPECT_EQ(1.2_c * 2, 2.4_c);
  EXPECT_EQ(1.8_c * 2, 3.6_c);
  EXPECT_EQ((-1.8_c) * 2, -3.6_c);
  EXPECT_EQ(1.8_c * (-2), -3.6_c);
  EXPECT_EQ((-1.8_c) * (-2), 3.6_c);
  EXPECT_EQ((-0.5_c) * 10, -5_c);

  EXPECT_EQ(1_c * 1_c, 1_c);
  EXPECT_EQ(2_c * 2_c, 4_c);
  EXPECT_EQ((-2_c) * 2_c, -4_c);
  EXPECT_EQ(2_c * (-2_c), -4_c);
  EXPECT_EQ(-2_c * (-2_c), 4_c);
  EXPECT_EQ(1.1_c * 1.1_c, 1.21_c);
  EXPECT_EQ(0.1_c * 0.1_c, 0.01_c);
  EXPECT_EQ(1.23_c * 1.89_c, 2.3247_c);
  EXPECT_EQ(0.123456789_c * 0.987654321_c, 0.121932631112635269_c);
  EXPECT_EQ((-1.23_c) * 1.89_c, -2.3247_c);
  EXPECT_EQ(1.23_c * (-1.89_c), -2.3247_c);
  EXPECT_EQ((-1.23_c) * (-1.89_c), 2.3247_c);

  EXPECT_EQ(1_c * 1.0, 1_c);
  EXPECT_EQ(2_c * 2.0, 4_c);
  EXPECT_EQ((-2_c) * 2.0, -4_c);
  EXPECT_EQ(2_c * (-2.0), -4_c);
  EXPECT_EQ(-2_c * (-2.0), 4_c);
  EXPECT_EQ(1.1_c * 1.1, 1.21_c);
  EXPECT_EQ(0.1_c * 0.1, 0.01_c);
  EXPECT_EQ(1.23_c * 1.89, 2.3247_c);
  EXPECT_EQ(0.123456789_c * 0.987654321, 0.121932631112635269_c);
  EXPECT_EQ((-1.23_c) * 1.89, -2.3247_c);
  EXPECT_EQ(1.23_c * (-1.89), -2.3247_c);
  EXPECT_EQ((-1.23_c) * (-1.89), 2.3247_c);

  EXPECT_EQ(2 * 1.8_c , 3.6_c);
  EXPECT_EQ(2 * (-1.8_c), -3.6_c);
  EXPECT_EQ((-2) * 1.8_c, -3.6_c);
  EXPECT_EQ((-2) * (-1.8_c), 3.6_c);
  EXPECT_EQ(10 * (-0.5_c), -5_c);

  EXPECT_EQ(0.1 * 0.1_c, 0.01_c);
  EXPECT_EQ(1.23 * 1.89_c, 2.3247_c);
  EXPECT_EQ(0.123456789 * 0.987654321_c, 0.121932631112635269_c);
  EXPECT_EQ((-1.23) * 1.89_c, -2.3247_c);
  EXPECT_EQ(1.23 * (-1.89_c), -2.3247_c);
  EXPECT_EQ((-1.23) * (-1.89_c), 2.3247_c);
}

TEST_F(AmountTest, AssignmentMultiply)
{
  {
    Amount a = 1.1_c;
    a *= 2;
    EXPECT_EQ(a, 2.2_c);
  }

  {
    Amount a = 0.123456789_c;
    a *= 0.987654321_c;
    EXPECT_EQ(a, 0.121932631112635269_c);
  }

  {
    Amount a = 0.123456789_c;
    a *= -0.987654321_c;
    EXPECT_EQ(a, -0.121932631112635269_c);
  }
}

TEST_F(AmountTest, Division)
{
  EXPECT_THROW(Amount(1) / 0, std::overflow_error);

  EXPECT_EQ(1.23_c / 1, 1.23_c);
  EXPECT_EQ(1.23_c / (-1), -1.23_c);
  EXPECT_EQ((-1.23_c) / (-1), 1.23_c);

  EXPECT_EQ(10_c / 2, 5_c);
  EXPECT_EQ(3.2_c / 2, 1.6_c);
  EXPECT_EQ(10_c / 3, 3.333333333333333333_c);
  EXPECT_EQ(12345.6789_c * 99 / 100, 12222.222111_c);

  {
    constexpr const Amount a{12345678.1234567891234_c};
    constexpr const Amount p1(a * 17 / 100);
    constexpr const Amount p2(a * 83 / 100);
    EXPECT_EQ(p1 + p2, a);
  }
}

TEST_F(AmountTest, AssignmentDivision)
{
  {
    Amount a = 1.1_c;
    EXPECT_THROW(a /= 0, std::overflow_error);
  }

  {
    Amount a = 1.1_c;
    a /= 1;
    EXPECT_EQ(a, 1.1_c);
  }

  {
    Amount a = 1.1_c;
    a /= 2;
    EXPECT_EQ(a, 0.55_c);
  }
}

TEST_F(AmountTest, ToString)
{
  EXPECT_EQ((0_c).to_string(), "0.00");
  EXPECT_EQ((0_c).to_string(0), "0");
  EXPECT_EQ((0_c).to_string(18), "0.000000000000000000");

  EXPECT_EQ((1_c).to_string(), "1.00");
  EXPECT_EQ((1_c).to_string(0), "1");
  EXPECT_EQ((1_c).to_string(18), "1.000000000000000000");
  EXPECT_EQ((1.1_c).to_string(), "1.10");
  EXPECT_EQ((1.01_c).to_string(), "1.01");
  EXPECT_EQ((1.001_c).to_string(), "1.001");
  EXPECT_EQ((1.0001_c).to_string(), "1.0001");
  EXPECT_EQ((1.00001_c).to_string(), "1.00001");

  EXPECT_EQ((-1_c).to_string(), "-1.00");
  EXPECT_EQ((-1_c).to_string(0), "-1");
  EXPECT_EQ((-1_c).to_string(18), "-1.000000000000000000");
  EXPECT_EQ((-1.1_c).to_string(), "-1.10");
  EXPECT_EQ((-1.01_c).to_string(), "-1.01");
  EXPECT_EQ((-1.001_c).to_string(), "-1.001");
  EXPECT_EQ((-1.0001_c).to_string(), "-1.0001");
  EXPECT_EQ((-1.00001_c).to_string(), "-1.00001");

  EXPECT_EQ((1234567891_c).to_string(), "1234567891.00");
  EXPECT_EQ((1234567891_c).to_string(0), "1234567891");
  EXPECT_EQ((-1234567891_c).to_string(), "-1234567891.00");
  EXPECT_EQ((-1234567891_c).to_string(0), "-1234567891");

  EXPECT_EQ((1234567891.12345678912345678_c).to_string(), "1234567891.12345678912345678");
  EXPECT_EQ((1234567891.12345678912345678_c).to_string(0), "1234567891.12345678912345678");
  EXPECT_EQ((-1234567891.12345678912345678_c).to_string(), "-1234567891.12345678912345678");
  EXPECT_EQ((-1234567891.12345678912345678_c).to_string(0), "-1234567891.12345678912345678");
}
