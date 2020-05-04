#define TESTING

#include <solver/smartcontracts.hpp>
#include "clientconfigmock.hpp"
#include "gtest/gtest.h"
#include <csdb/amount.hpp>
#include <csdb/amount_commission.hpp>

constexpr std::array<std::tuple<size_t, double, double>, 14> feeLevels = {
    std::make_tuple(5 * 1024,    0.087402,  0.008740), // this is min fee value
    std::make_tuple(20 * 1024,   0.174805,  0.034961),
    std::make_tuple(50 * 1024,   0.437500,  0.139648),
    std::make_tuple(100 * 1024,  17.87109,  17.87109),
    std::make_tuple(256 * 1024,  89.55078,  89.55078),
    std::make_tuple(512 * 1024,  358.39844, 358.39844),
    std::make_tuple(768 * 1024,  1435.5469, 1435.5469),
    std::make_tuple(1024 * 1024, 5732.4219, 5732.4219),
    std::make_tuple(5 * 1024 * 1024,    22949.21875, 22949.21875),
    std::make_tuple(15 * 1024 * 1024,   91699.21875, 91699.21875),
    std::make_tuple(50 * 1024 * 1024,   367187.5,    367187.5),
    std::make_tuple(100 * 1024 * 1024,  1464843.75,  1464843.75),
    std::make_tuple(500 * 1024 * 1024,  5869140.625, 5869140.625),
    std::make_tuple(1000 * 1024 * 1024, 23437500.0,  23437500.0)
};

TEST(Amount, to_double) {
    csdb::Amount val{0, 18000000000000000};
    ASSERT_EQ(val.to_double(), 0.018);

    std::cout << "transfer / call" << std::endl;
    for (int i = 0; i < 14; ++i) {
        std::cout << i + 1 << " <" << std::get<0>(feeLevels[i]) << '\t';
        csdb::AmountCommission comm(std::get<2>(feeLevels[i]));
        std::cout << comm.get_raw() << '\t' << comm.to_double() << '\t';
        csdb::Amount amount(comm.to_double());
        std::cout << amount.to_string() << " (" << amount.integral() << '.' << std::setfill('0') << std::setw(18) << amount.fraction() << ')';
        std::cout << std::endl;
    }

    std::cout << "deployment" << std::endl;
    for (int i = 0; i < 14; ++i) {
        std::cout << i + 1 << " <" << std::get<0>(feeLevels[i]) << '\t';
        csdb::AmountCommission comm(std::get<1>(feeLevels[i]));
        std::cout << comm.get_raw() << '\t' << comm.to_double() << '\t';
        csdb::Amount amount(comm.to_double());
        std::cout << amount.to_string() << " (" << amount.integral() << '.' << std::setfill('0') << std::setw(18) << amount.fraction() << ')';
        std::cout << std::endl;
    }
}
