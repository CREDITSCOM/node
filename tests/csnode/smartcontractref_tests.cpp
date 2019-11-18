#define TESTING

#include <solver/smartcontracts.hpp>
#include "clientconfigmock.hpp"
#include "gtest/gtest.h"
#include "networkmock.hpp"
#include "nodemock.hpp"
#include "solvermock.hpp"
#include "transportmock.hpp"

TEST(SmartContractRef, constructors) {
    cs::SmartContractRef r1;
    ASSERT_FALSE(r1.is_valid());

    r1.sequence = 0;
    r1.transaction = 0;
    ASSERT_TRUE(r1.is_valid());

    cs::SmartContractRef r2(0, 1);
    ASSERT_TRUE(r2.is_valid());
}

TEST(SmartContractRef, comparisons) {
    cs::SmartContractRef ref_1_0(1, 0);
    cs::SmartContractRef ref_1_1(1, 1);
    cs::SmartContractRef ref_2_0(2, 0);
    cs::SmartContractRef ref_2_1(2, 1);
    cs::SmartContractRef ref_invalid;

    ASSERT_GT(ref_1_1, ref_1_0);
    ASSERT_LT(ref_1_0, ref_1_1);

    ASSERT_GT(ref_2_0, ref_1_0);
    ASSERT_GT(ref_2_0, ref_1_1);
    ASSERT_LT(ref_1_0, ref_2_0);
    ASSERT_LT(ref_1_1, ref_2_0);

    ASSERT_GT(ref_2_1, ref_1_0);
    ASSERT_GT(ref_2_1, ref_1_1);
    ASSERT_GT(ref_2_1, ref_2_0);
    ASSERT_LT(ref_1_0, ref_2_1);
    ASSERT_LT(ref_1_1, ref_2_1);
    ASSERT_LT(ref_2_0, ref_2_1);

    ASSERT_LT(ref_invalid, ref_1_0);
    ASSERT_FALSE(ref_invalid < ref_invalid);
    ASSERT_FALSE(ref_1_0 < ref_invalid);
    ASSERT_TRUE(ref_1_0 > ref_invalid);

    ASSERT_GT(ref_1_0, ref_invalid);
    ASSERT_FALSE(ref_invalid > ref_1_0);
    ASSERT_TRUE(ref_1_0 > ref_invalid);
    ASSERT_FALSE(ref_invalid > ref_invalid);

    ASSERT_EQ(ref_1_0, ref_1_0);
    ASSERT_EQ(ref_invalid, ref_invalid);
    ASSERT_FALSE(ref_1_0 == ref_1_1);
    ASSERT_FALSE(ref_1_0 == ref_2_0);
    ASSERT_FALSE(ref_invalid == ref_1_0);
    ASSERT_FALSE(ref_2_0 == ref_1_0);
    ASSERT_FALSE(ref_2_0 == ref_invalid);
}