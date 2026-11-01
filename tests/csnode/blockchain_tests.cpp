#define TESTING

#include "clientconfigmock.hpp"
#include "gtest/gtest.h"

#include <csnode/blockchain.hpp>

#include <csdb/pool.hpp>

TEST(BlockChain, block_service_info) {
    csdb::Pool block{};

    ASSERT_FALSE(BlockChain::isBootstrap(block));
    BlockChain::setBootstrap(block, false);
    ASSERT_FALSE(BlockChain::isBootstrap(block));
    BlockChain::setBootstrap(block, true);
    ASSERT_TRUE(BlockChain::isBootstrap(block));
    BlockChain::setBootstrap(block, false);
    ASSERT_FALSE(BlockChain::isBootstrap(block));

    block = csdb::Pool{};

    ASSERT_FALSE(BlockChain::isBootstrap(block));
    BlockChain::setBootstrap(block, true);
    ASSERT_TRUE(BlockChain::isBootstrap(block));
    BlockChain::setBootstrap(block, true);
    ASSERT_TRUE(BlockChain::isBootstrap(block));
    BlockChain::setBootstrap(block, false);
    ASSERT_FALSE(BlockChain::isBootstrap(block));
}

// The block rebuilt from this node's block and the writer's round package must be the writer's block,
// byte for byte (its hash is what the package's signers signed): mask, timestamp and the block reward
// come from the package, everything else, the bootstrap flag included, from this node's block
TEST(BlockChain, BlockFromPackageMetaIsTheWritersBlock) {
    const cs::PublicKey k1{1}, k2{2}, k3{3}, k4{4}, k5{5};
    auto base = [&](const cs::Bytes& mask, const std::string& timestamp, const std::string& reward) {
        csdb::Pool p;
        p.set_sequence(1433);
        p.set_previous_hash(csdb::PoolHash::calc_from_data(cs::Bytes{1, 2, 3}));
        p.add_number_trusted(static_cast<uint8_t>(mask.size()));
        p.add_real_trusted(cs::Utils::maskToBits(mask));
        p.set_confidants({k1, k2, k3, k4, k5});
        p.setRoundCost(csdb::Amount(0));
        BlockChain::setTimestamp(p, timestamp);
        BlockChain::setBootstrap(p, true);
        if (!reward.empty()) {
            p.add_user_field(BlockChain::kFieldBlockReward, reward);
        }
        p.add_number_confirmations(3);
        p.add_confirmation_mask(7);
        p.add_round_confirmations({cs::Signature{}, cs::Signature{}, cs::Signature{}});
        return p;
    };
    const cs::Bytes writerMask{0, 1, 255, 255, 2};
    csdb::Pool writer = base(writerMask, "1792732732167", "reward-of-three");
    csdb::Pool own = base(cs::Bytes{0, 1, 2, 3, 4}, "1792732732100", "reward-of-five");
    ASSERT_TRUE(writer.compose());

    cs::PoolMetaInfo meta;
    meta.sequenceNumber = 1433;
    meta.previousHash = writer.previous_hash();
    meta.realTrustedMask = writerMask;
    meta.timestamp = "1792732732167";
    meta.reward = "reward-of-three";

    csdb::Pool rebuilt = BlockChain::blockFromPackageMeta(own, meta);
    ASSERT_TRUE(rebuilt.compose());
    EXPECT_EQ(rebuilt.hash(), writer.hash());
    EXPECT_TRUE(BlockChain::isBootstrap(rebuilt));

    // without a reward on either side
    csdb::Pool writerNoReward = base(writerMask, "1792732732167", "");
    ASSERT_TRUE(writerNoReward.compose());
    meta.reward.clear();
    csdb::Pool rebuiltNoReward = BlockChain::blockFromPackageMeta(base(cs::Bytes{0, 1, 2, 3, 4}, "1", "reward"), meta);
    ASSERT_TRUE(rebuiltNoReward.compose());
    EXPECT_EQ(rebuiltNoReward.hash(), writerNoReward.hash());
}
