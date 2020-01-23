#define TESTING

#include <solver/smartcontracts.hpp>
#include "clientconfigmock.hpp"
#include "gtest/gtest.h"
#include <csdb/amount.hpp>

TEST(Amount, to_double) {
    csdb::Amount val{0, 18000000000000000};
    ASSERT_EQ(val.to_double(), 0.018);
}