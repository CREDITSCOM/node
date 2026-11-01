#define TESTING

#include "clientconfigmock.hpp"
#include "gtest/gtest.h"

#include <csnode/blockchain.hpp>

#include <csdb/address.hpp>
#include <csdb/amount.hpp>

namespace {
const csdb::Address kGenesis = csdb::Address::from_string("0000000000000000000000000000000000000000000000000000000000000001");
const csdb::Address kStart = csdb::Address::from_string("0000000000000000000000000000000000000000000000000000000000000002");
}  // namespace

// mining and staking are toggled independently (special transaction order 37) and
// restored from quick-start caches through these accessors
TEST(BlockChainSettings, MiningAndStakingSettersAreIndependent) {
    BlockChain blockChain(kGenesis, kStart);

    blockChain.setMiningOn(true);
    blockChain.setStakingOn(false);
    ASSERT_TRUE(blockChain.getMiningOn());
    ASSERT_FALSE(blockChain.getStakingOn());

    blockChain.setMiningOn(false);
    blockChain.setStakingOn(true);
    ASSERT_FALSE(blockChain.getMiningOn());
    ASSERT_TRUE(blockChain.getStakingOn());
}

// these members are saved into quick-start caches and read back on the --set-bc-top path,
// possibly before any consensus-settings change has set them
TEST(BlockChainSettings, ConsensusSettingsHaveDefinedDefaults) {
    BlockChain blockChain(kGenesis, kStart);

    ASSERT_TRUE(blockChain.getBlockReward() == csdb::Amount{ 0 });
    ASSERT_TRUE(blockChain.getMiningCoefficient() == csdb::Amount{ 0 });
    ASSERT_FALSE(blockChain.getMiningOn());
    ASSERT_FALSE(blockChain.getStakingOn());
    ASSERT_EQ(blockChain.getTimeMinStage1(), 500u);
}
