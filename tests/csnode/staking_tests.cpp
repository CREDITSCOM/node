#include <gtest/gtest.h>

#include <map>

#include <csnode/nodecore.hpp>
#include <csnode/staking.hpp>

// a block with a timed delegation may be reverted (e.g. an uncertain last block) and applied again;
// WalletsCache reverts the target side first, then the source side
namespace {
struct Wallets {
    std::map<cs::PublicKey, cs::WalletsCache::WalletData> data;

    cs::WalletsCache::WalletData get(const cs::PublicKey& key) {
        auto& w = data[key];
        w.key_ = key;
        return w;
    }
    void put(const cs::WalletsCache::WalletData& w) {
        data[w.key_] = w;
    }
};

cs::PublicKey key(uint8_t b) {
    cs::PublicKey k{};
    k.fill(b);
    return k;
}

constexpr uint64_t kStartMs = 1'700'000'000'000ULL;          // block time, ms
#define ASSERT_AMOUNT(actual, expected) ASSERT_TRUE((actual) == (expected)) << (actual).to_string()

constexpr uint64_t kExpiry = 1'700'000'000ULL + 10'000'000;  // delegation end, s (> 90 days ahead)
}  // namespace

TEST(Staking, RevertOfTimedDelegationRefundsSource) {
    Wallets wallets;
    cs::Staking staking([&](const cs::PublicKey& k) { return wallets.get(k); },
                        [&](const cs::WalletsCache::WalletData& w) { wallets.put(w); });

    const auto source = key(1);
    const auto target = key(2);
    wallets.data[source].balance_ = csdb::Amount{ 1000 };

    const csdb::UserField timed{ kExpiry };
    const csdb::Amount amount{ 300 };
    const csdb::TransactionID id(100, 0);

    staking.addDelegationsForTarget(timed, source, target, amount, id, kStartMs);
    staking.addDelegationsForSource(timed, source, target, amount, kStartMs);
    ASSERT_AMOUNT(wallets.data[source].balance_, csdb::Amount{ 700 });
    ASSERT_AMOUNT(wallets.data[target].delegated_, amount);

    staking.revertDelegationsForTarget(timed, source, target, amount, id, kStartMs);
    staking.revertDelegationsForSource(timed, source, target, amount, id, kStartMs);

    ASSERT_AMOUNT(wallets.data[source].balance_, csdb::Amount{ 1000 });
    ASSERT_AMOUNT(wallets.data[target].delegated_, csdb::Amount{ 0 });
    ASSERT_TRUE(staking.getCurrentDelegations().empty() || staking.getCurrentDelegations().begin()->second.empty());
    ASSERT_EQ(staking.getMiningDelegations(target), nullptr);

    // applying the block again must give the same state as the first time
    staking.addDelegationsForTarget(timed, source, target, amount, id, kStartMs);
    staking.addDelegationsForSource(timed, source, target, amount, kStartMs);
    ASSERT_AMOUNT(wallets.data[source].balance_, csdb::Amount{ 700 });
    ASSERT_AMOUNT(wallets.data[target].delegated_, amount);
    ASSERT_EQ(staking.getMiningDelegations(target)->size(), 1u);
}
