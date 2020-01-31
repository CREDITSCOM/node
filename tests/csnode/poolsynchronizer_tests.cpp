#include <gtest/gtest.h>

#include <csnode/blockchain.hpp>
#include <csnode/poolsynchronizer.hpp>

#include <lib/system/random.hpp>

const csdb::Address genesisAddress = csdb::Address::from_string("0000000000000000000000000000000000000000000000000000000000000001");
const csdb::Address startAddress = csdb::Address::from_string("0000000000000000000000000000000000000000000000000000000000000002");

[[maybe_unused]] static std::vector<std::string> base58Keys = {
    "4tEQbQPYZq1bZ8Tn9DpCXYUgPgEgcqsBPXX4fXef7FuL",
    "CzvqNvaQpsE2v4FFTJ2mnTKu48JsMhLzzMALja8bt7DD",
    "G2GeLfwjg6XuvoWnZ7ssx9EPkEBqbYL3mw3fusgpzoBk",
    "HYNKVYEC5pKAAgoJ56WdvbuzJBpjZtGBvpSMBeVd555S",
    "FBpAc4VDxVk1oDLK73xYmCwMJC2XA9zGyXjxJeN15KuD",
    "132jBPFmZhWn4siSAKXHztXizTRmBVjTuy7ejwiW3jcu",
    "4VYZzAWuT4K5iti9Xjckyb196jtu9oaCnrkAgXHHt7t6",
    "DDhaSqP3U67Cjs1fm7HtdFpErZUgMJ4CCZqgzdxYGoRe"
};

class PoolSynchronizerTest : public cs::PoolSynchronizer {
public:
    PoolSynchronizerTest(BlockChain* blockchain):PoolSynchronizer(blockchain) {}

    bool isCorrect() const {
        auto n = neighbours();
        return std::is_sorted(std::begin(n), std::end(n), [](const auto& lhs, const auto& rhs) {
            return lhs.second > rhs.second;
        });
    }
};

static std::unique_ptr<PoolSynchronizerTest> createPoolSynchronizer() {
    static BlockChain blockChain(genesisAddress, startAddress);
    return std::make_unique<PoolSynchronizerTest>(&blockChain);
}

static std::map<cs::PublicKey, cs::Sequence> createKeys() {
    static const size_t keysCount = 100;
    std::map<cs::PublicKey, cs::Sequence> keys;

    for (size_t i = 0; i < keysCount; ++i) {
        cs::PublicKey key;
        cscrypto::generateKeyPair(key);

        keys.emplace(key, 1);
    }

    return keys;
}

TEST(PoolSynchronizer, TestNeighbours) {
    static const size_t iterations = 1000;

    auto synchronizer = createPoolSynchronizer();
    auto keys = createKeys();

    for (const auto& [key, value] : keys) {
        synchronizer->onNeighbourAdded(key, value);
    }

    for (size_t i = 0; i < iterations; ++i) {
        auto index = cs::Random::generateValue<size_t>(0, 99);
        auto iter = std::next(keys.begin(), static_cast<std::ptrdiff_t>(index));
        auto sequenceStep = cs::Random::generateValue<cs::Sequence>(10, 1000);

        iter->second += sequenceStep;

        ASSERT_TRUE(synchronizer->isCorrect());
    }
}
