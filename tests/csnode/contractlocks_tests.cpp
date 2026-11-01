#define TESTING

#include <solver/smartcontracts.hpp>
#include "clientconfigmock.hpp"
#include "gtest/gtest.h"

#include <unordered_set>
#include <vector>

// a contract left locked by an aborted call is unlocked, while the contracts that
// a running call executes or uses stay locked
TEST(SmartContract, stale_locks_keeps_used_contracts) {
    using namespace cs;
    auto address = [](uint8_t n) {
        cs::PublicKey key{};
        key[0] = n;
        return csdb::Address::from_public_key(key);
    };
    const auto running = address(1);
    const auto used = address(2);
    const auto stale = address(3);

    const std::unordered_set<csdb::Address> locked{running, used, stale};
    const auto result = SmartContracts::stale_locks(locked, {running, used});
    ASSERT_EQ(result.size(), 1u);
    ASSERT_EQ(result.front(), stale);

    ASSERT_TRUE(SmartContracts::stale_locks(locked, {running, used, stale}).empty());
    ASSERT_EQ(SmartContracts::stale_locks(locked, {address(9)}).size(), 3u);
}
