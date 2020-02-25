#include <stage.hpp>
#include <solvercontext.hpp>
#include <vector>
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
