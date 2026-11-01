#include "gtest/gtest.h"

#include <csnode/blocktakeover.hpp>

using cs::LastBlockVerdict;
using cs::lastBlockVerdict;

namespace {
constexpr size_t kMinTrusted = 4;  // Consensus::MinTrustedNodes
const cs::Bytes all5{0, 1, 2, 3, 4};
const cs::Bytes three5{0, 1, 255, 255, 2};   // the retry without two late nodes
const cs::Bytes two5{0, 255, 255, 255, 1};
}  // namespace

TEST(BlockTakeover, SameSetIsTheSameBlock) {
    EXPECT_EQ(lastBlockVerdict(all5, cs::Bytes{4, 3, 2, 1, 0}, kMinTrusted), LastBlockVerdict::Same);
    EXPECT_EQ(lastBlockVerdict(three5, three5, kMinTrusted), LastBlockVerdict::Same);
}

// the run that forked (#1433): the writer's block signed by 3 of 5, the node's own by all 5
TEST(BlockTakeover, TheFullSideTakesTheWritersMajorityBlock) {
    EXPECT_EQ(lastBlockVerdict(all5, three5, kMinTrusted), LastBlockVerdict::Take);
    // and the other way round the official rule applies
    EXPECT_EQ(lastBlockVerdict(three5, all5, kMinTrusted), LastBlockVerdict::MoreSignatures);
}

TEST(BlockTakeover, NeverWithoutAMajorityOfTheRound) {
    EXPECT_EQ(lastBlockVerdict(all5, two5, kMinTrusted), LastBlockVerdict::KeepNoMajority);
    EXPECT_EQ(lastBlockVerdict(cs::Bytes{0, 1, 2, 3}, cs::Bytes{0, 1, 255, 255}, kMinTrusted), LastBlockVerdict::KeepNoMajority);
}

// a larger round: the nodes outside the writer's set could still form a round on their block
TEST(BlockTakeover, NotWhileTheOtherSetCouldFormARound) {
    cs::Bytes own9(9, 0);
    cs::Bytes five9{0, 1, 2, 3, 4, 255, 255, 255, 255};
    cs::Bytes six9{0, 1, 2, 3, 4, 5, 255, 255, 255};
    EXPECT_EQ(lastBlockVerdict(own9, five9, kMinTrusted), LastBlockVerdict::KeepOtherSideMayWin);
    EXPECT_EQ(lastBlockVerdict(own9, six9, kMinTrusted), LastBlockVerdict::Take);
}

TEST(BlockTakeover, MasksOfDifferentRounds) {
    EXPECT_EQ(lastBlockVerdict(all5, cs::Bytes{0, 1, 2}, kMinTrusted), LastBlockVerdict::KeepSizeMismatch);
    EXPECT_EQ(lastBlockVerdict(cs::Bytes{}, cs::Bytes{}, kMinTrusted), LastBlockVerdict::KeepSizeMismatch);
}

TEST(BlockTakeover, OnlyTakeAndMoreSignaturesLetThePackageWin) {
    EXPECT_TRUE(cs::packageBlockWins(LastBlockVerdict::Take));
    EXPECT_TRUE(cs::packageBlockWins(LastBlockVerdict::MoreSignatures));
    EXPECT_FALSE(cs::packageBlockWins(LastBlockVerdict::Same));
    EXPECT_FALSE(cs::packageBlockWins(LastBlockVerdict::KeepNoMajority));
    EXPECT_FALSE(cs::packageBlockWins(LastBlockVerdict::KeepOtherSideMayWin));
}

// the smallest and the in-between rounds: Take needs a majority and fewer than MinTrustedNodes outside
TEST(BlockTakeover, RoundSizeBoundaries) {
    EXPECT_EQ(lastBlockVerdict(cs::Bytes(4, 0), cs::Bytes{0, 1, 2, 255}, kMinTrusted), LastBlockVerdict::Take);
    EXPECT_EQ(lastBlockVerdict(cs::Bytes(6, 0), cs::Bytes{0, 1, 2, 255, 255, 255}, kMinTrusted), LastBlockVerdict::KeepNoMajority);
    EXPECT_EQ(lastBlockVerdict(cs::Bytes(6, 0), cs::Bytes{0, 1, 2, 3, 255, 255}, kMinTrusted), LastBlockVerdict::Take);
    EXPECT_EQ(lastBlockVerdict(cs::Bytes(7, 0), cs::Bytes{0, 1, 2, 3, 255, 255, 255}, kMinTrusted), LastBlockVerdict::Take);
    EXPECT_EQ(lastBlockVerdict(cs::Bytes(8, 0), cs::Bytes{0, 1, 2, 3, 4, 255, 255, 255}, kMinTrusted), LastBlockVerdict::Take);
    EXPECT_EQ(lastBlockVerdict(cs::Bytes(8, 0), cs::Bytes{0, 1, 2, 3, 255, 255, 255, 255}, kMinTrusted), LastBlockVerdict::KeepNoMajority);
}
