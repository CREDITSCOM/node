#include "gtest/gtest.h"

#include <limits>

#include <csnode/bootstrapgate.hpp>

using cs::bootstrapStallThresholdMs;
using cs::isBootstrapRoundInWindow;
using cs::isBootstrapStallAccepted;

// the rounds an honest starter sends to a receiver: its last block + 1, the receiver being level with
// it, one block ahead or behind, or still fetching blocks with the network round learned
TEST(BootstrapGate, HonestRoundsAreInTheWindow) {
    EXPECT_TRUE(isBootstrapRoundInWindow(1, 0, 0));          // a fresh network
    EXPECT_TRUE(isBootstrapRoundInWindow(501, 500, 501));    // level with the starter
    EXPECT_TRUE(isBootstrapRoundInWindow(500, 500, 501));    // one block the starter lacks
    EXPECT_TRUE(isBootstrapRoundInWindow(502, 500, 500));    // lacks the starter's last block
    EXPECT_TRUE(isBootstrapRoundInWindow(9001, 100, 9001));  // fetching blocks, round learned
    EXPECT_TRUE(isBootstrapRoundInWindow(9002, 100, 9001));  // and one round table missed
    EXPECT_TRUE(isBootstrapRoundInWindow(9003, 100, 9001));
    EXPECT_TRUE(isBootstrapRoundInWindow(500, 499, 501));    // the round moves down by one
    EXPECT_TRUE(isBootstrapRoundInWindow(500, 500, 0));      // restarted, round not known yet
    EXPECT_TRUE(isBootstrapRoundInWindow(502, 500, 0));
}

TEST(BootstrapGate, OtherRoundsAreNot) {
    EXPECT_FALSE(isBootstrapRoundInWindow(0, 0, 0));
    EXPECT_FALSE(isBootstrapRoundInWindow(499, 500, 501));   // below the last block
    EXPECT_FALSE(isBootstrapRoundInWindow(1, 500, 501));
    EXPECT_FALSE(isBootstrapRoundInWindow(503, 500, 0));
    EXPECT_FALSE(isBootstrapRoundInWindow(9004, 100, 9001));
    EXPECT_FALSE(isBootstrapRoundInWindow(1000000, 500, 501));
}

TEST(BootstrapGate, NoOverflowNearTheTop) {
    const auto max = std::numeric_limits<cs::RoundNumber>::max();
    EXPECT_TRUE(isBootstrapRoundInWindow(max, max - 1, max - 1));
    EXPECT_FALSE(isBootstrapRoundInWindow(max, 0, 0));
    EXPECT_TRUE(isBootstrapRoundInWindow(max, max, max));
}

TEST(BootstrapGate, StallThresholdIsHalfTheElapseTime) {
    EXPECT_EQ(bootstrapStallThresholdMs(10000), 5000u);
    EXPECT_EQ(bootstrapStallThresholdMs(60000), 30000u);
    EXPECT_EQ(bootstrapStallThresholdMs(0), 0u);
}

TEST(BootstrapGate, AStalledRoundTakesTheTable) {
    const uint64_t up = 3600000;    // started an hour ago
    EXPECT_FALSE(isBootstrapStallAccepted(0, up, 10000, false));
    EXPECT_FALSE(isBootstrapStallAccepted(4999, up, 10000, false));
    EXPECT_TRUE(isBootstrapStallAccepted(5000, up, 10000, false));
    EXPECT_TRUE(isBootstrapStallAccepted(60000, up, 60000, false));
    EXPECT_TRUE(isBootstrapStallAccepted(0, up, 0, false));
}

// a node younger than round_elapse_time, or one already restarting with the others, takes it as the
// official node does
TEST(BootstrapGate, YoungOrBootstrappingNodesTakeTheTable) {
    EXPECT_TRUE(isBootstrapStallAccepted(0, 9999, 10000, false));
    EXPECT_FALSE(isBootstrapStallAccepted(0, 10000, 10000, false));
    EXPECT_TRUE(isBootstrapStallAccepted(0, 3600000, 10000, true));
}
