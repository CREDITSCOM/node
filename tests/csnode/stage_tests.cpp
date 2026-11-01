#include <stage.hpp>
#include <solvercontext.hpp>
#include <limits>
#include <vector>
#include <csnode/datastream.hpp>
#include <nodecore.hpp>


#include "gtest/gtest.h"


TEST(StageOne, Constructors) {

    cs::StageOne fake;

    //init_zero(fake):
    fake.sender = 255;
    fake.hash.fill(0);
    fake.messageHash.fill(0);
    fake.signature.fill(0);
    fake.hashesCandidates.clear();
    fake.trustedCandidates.clear();
    fake.roundTimeStamp.clear();

    fake.sender = 1U;
    fake.toBytes();

    ASSERT_TRUE(fake.message.size() > 0);
}
TEST(StageTwo, Constructors) {

    cs::StageTwo fake;

    //init_zero(fake):
    fake.sender = cs::InvalidSender;
    fake.signature.fill(0);
    fake.hashes.resize(3, cs::Zero::hash);
    fake.signatures.resize(3, cs::Zero::signature);

    fake.sender = 1U;
    fake.toBytes();

    ASSERT_TRUE(fake.message.size() > 0);
}

TEST(StageThree, Constructors) {

    cs::StageThree fake;
    fake.sender = 1U;
    fake.iteration = 0U;
    fake.blockSignature = cs::Zero::signature;
    fake.roundSignature = cs::Zero::signature;
    fake.trustedSignature = cs::Zero::signature;
 
    fake.toBytes();

    ASSERT_TRUE(fake.message.size() > 0);
}

namespace {
cs::Bytes smartStageOneMessage(size_t declaredFees, size_t actualFees) {
    cs::Bytes message;
    cs::ODataStream out(message);
    out << uint8_t(1) << uint64_t(42) << declaredFees;
    for (size_t i = 0; i < actualFees; ++i) {
        out << int32_t(1) << uint64_t(0);
    }
    cs::Hash hash{};
    hash.fill(7);
    out << hash;
    return message;
}
}  // namespace

TEST(StageOneSmarts, ReadsItsFees) {
    cs::StageOneSmarts stage;
    stage.message = smartStageOneMessage(5, 5);
    ASSERT_TRUE(stage.fillFromBinary());
    EXPECT_EQ(stage.fees.size(), 5u);
    EXPECT_EQ(stage.hash[0], 7);
}

TEST(StageOneSmarts, AFeeCountBeyondTheMessageIsRefused) {
    for (size_t declared : {size_t(6), size_t(1000), std::numeric_limits<size_t>::max()}) {
        cs::StageOneSmarts stage;
        stage.message = smartStageOneMessage(declared, 5);
        EXPECT_FALSE(stage.fillFromBinary()) << declared;
        EXPECT_LE(stage.fees.size(), 7u);   // never more than the message can hold
    }
}
