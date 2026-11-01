#include "gtest/gtest.h"

#include <algorithm>
#include <initializer_list>
#include <set>

#include <csnode/bootstrapweight.hpp>

using cs::BootstrapWeight;
using cs::bootstrapWeight;

namespace {
// keys whose set order is their index
cs::PublicKey key(int i) {
    cs::PublicKey k{};
    k[0] = static_cast<uint8_t>(i);
    k[31] = static_cast<uint8_t>(0xA0 + i);
    return k;
}

std::set<cs::PublicKey> initial(int count) {
    std::set<cs::PublicKey> s;
    for (int i = 0; i < count; ++i) {
        s.insert(key(i));
    }
    return s;
}

cs::PublicKeys table(std::initializer_list<int> indices) {
    cs::PublicKeys t;
    for (int i : indices) {
        t.push_back(key(i));
    }
    return t;
}

cs::PublicKeys range(int from, int to) {
    cs::PublicKeys t;
    for (int i = from; i <= to; ++i) {
        t.push_back(key(i));
    }
    return t;
}

// the official Node::calculateBootStrapWeight as it behaves on x86-64: a shift count wrapped below 0
// is masked to 5 bits, and the uint8_t sum keeps the low byte, so those terms add nothing. The past-the-
// end read of the official loop is replaced by a bounds check (it can only add such a term)
uint8_t official(const std::set<cs::PublicKey>& init, const cs::PublicKeys& t) {
    size_t confSize = t.size();
    uint8_t weight = 0;
    if (confSize > 8) {
        return weight;
    }
    auto it = t.cbegin();
    --confSize;
    for (const auto& c : init) {
        if (it != t.cend() && std::equal(c.cbegin(), c.cend(), it->cbegin())) {
            weight = static_cast<uint8_t>(weight + (uint32_t(1) << (confSize & 31)));
            ++it;
        }
        --confSize;
    }
    return weight;
}
}  // namespace

// the values the official node gives: a community node must choose the same table
TEST(BootstrapWeight, OfficialValuesUpToEightConfidants) {
    struct Case {
        int init;
        cs::PublicKeys table;
        BootstrapWeight weight;
    };
    const Case cases[] = {
        {4, range(0, 3), 15},
        {5, range(0, 4), 31},
        {5, range(0, 3), 15},
        {5, range(1, 4), 7},
        {6, range(0, 5), 63},
        {6, range(0, 4), 31},
        {6, range(1, 5), 15},
        {6, range(0, 3), 15},
        {6, range(2, 5), 3},
        {7, range(0, 6), 127},
        {7, range(0, 5), 63},
        {7, range(1, 6), 31},
        {7, range(0, 4), 31},
        {7, range(2, 6), 7},
        {7, table({0, 2, 4, 6}), 10},
        {7, table({1, 3, 5}), 2},
        {7, range(3, 6), 1},
        {7, range(0, 3), 15},
        {7, table({0, 1, 2, 5, 6}), 28},
        {7, table({1, 0, 2, 3, 4}), 8},
        {7, table({6, 5, 4, 3, 2}), 0},
        {8, range(0, 7), 255},
        {8, range(0, 6), 127},
        {8, range(1, 7), 63},
        {8, range(4, 7), 0},
        {8, range(0, 4), 31},
        {8, range(3, 7), 3},
        {9, range(0, 7), 255},
        {9, range(1, 8), 127},
        {12, range(0, 3), 15},
        {12, range(8, 11), 0},
    };
    for (const auto& c : cases) {
        EXPECT_EQ(bootstrapWeight(initial(c.init), c.table), c.weight) << c.init << " initial, " << c.table.size() << " keys";
    }
}

// every table of up to 8 keys taken from up to 12 initial confidants, in order and reversed
TEST(BootstrapWeight, SameAsTheOfficialNodeForEveryTableUpToEightKeys) {
    for (int m = 1; m <= 12; ++m) {
        const auto init = initial(m);
        for (uint32_t mask = 1; mask < (1u << m); ++mask) {
            cs::PublicKeys t;
            for (int i = 0; i < m; ++i) {
                if (mask >> i & 1) {
                    t.push_back(key(i));
                }
            }
            if (t.size() > 8) {
                continue;
            }
            ASSERT_EQ(bootstrapWeight(init, t), official(init, t)) << m << " initial, mask " << mask;
            std::reverse(t.begin(), t.end());
            ASSERT_EQ(bootstrapWeight(init, t), official(init, t)) << m << " initial, reversed mask " << mask;
        }
    }
}

// a stall recovery on mainnet (7 initial confidants) with only some of them ready
TEST(BootstrapWeight, PartialTablesOfSeven) {
    const auto init = initial(7);
    EXPECT_EQ(bootstrapWeight(init, range(0, 4)), 31u);
    EXPECT_EQ(bootstrapWeight(init, range(2, 6)), 7u);
    EXPECT_EQ(bootstrapWeight(init, range(0, 5)), 63u);
    EXPECT_EQ(bootstrapWeight(init, range(1, 6)), 31u);
}

// the official node returns 0 from 9 keys on and drops the table: 9 initial confidants never bootstrap
TEST(BootstrapWeight, LargerTablesArePositiveAndGrowWithTheirSize) {
    BootstrapWeight previous = bootstrapWeight(initial(8), range(0, 7));
    for (int m = 9; m <= 25; ++m) {
        const auto full = bootstrapWeight(initial(m), range(0, m - 1));
        EXPECT_EQ(full, (BootstrapWeight(1) << (m + 1)) - 1) << m;
        EXPECT_GT(full, previous) << m;
        previous = full;
        // the lightest table of 9 keys still beats every table of 8
        EXPECT_GT(bootstrapWeight(initial(m), range(m - 9, m - 1)), 255u) << m;
    }
    EXPECT_GT(bootstrapWeight(initial(9), range(0, 8)), bootstrapWeight(initial(9), range(0, 7)));
}

// an unsigned table of 9 or more known keys counts only as the starter builds it: initial confidants
// in their order, each once; otherwise it would outweigh an honest table of up to 8
TEST(BootstrapWeight, LargerTablesOnlyAsTheStarterBuildsThem) {
    cs::PublicKeys repeated(9, key(0));
    EXPECT_EQ(bootstrapWeight(initial(7), repeated), 0u);
    EXPECT_EQ(bootstrapWeight(initial(25), repeated), 0u);
    auto padded = range(0, 6);
    padded.push_back(key(6));
    padded.push_back(key(6));
    EXPECT_EQ(bootstrapWeight(initial(7), padded), 0u);
    auto unordered = range(0, 8);
    std::swap(unordered[0], unordered[8]);
    EXPECT_EQ(bootstrapWeight(initial(9), unordered), 0u);
    auto foreign = range(0, 8);
    foreign[4] = key(200);
    EXPECT_EQ(bootstrapWeight(initial(9), foreign), 0u);
    EXPECT_GT(bootstrapWeight(initial(9), range(0, 8)), 255u);
}

TEST(BootstrapWeight, SignedBeatsEverything) {
    EXPECT_GT(cs::kSignedBootstrapWeight, bootstrapWeight(initial(25), range(0, 24)));
}

TEST(BootstrapWeight, EdgeCases) {
    EXPECT_EQ(bootstrapWeight(initial(7), cs::PublicKeys{}), 0u);
    EXPECT_EQ(bootstrapWeight(initial(26), range(0, 25)), 0u);       // more than MaxTrustedNodes
    // a key that is not an initial confidant is skipped, as by the official node
    auto t = range(0, 3);
    t.insert(t.begin() + 2, key(200));
    EXPECT_EQ(bootstrapWeight(initial(7), t), official(initial(7), t));
}
