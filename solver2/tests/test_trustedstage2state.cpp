#include <MockData.h>
#include <states/TrustedStage2State.h>

#include <gtest/gtest.h>

TEST(TrustedStage2State, TrivialMethods)
{
    using namespace ::testing;

    NiceMock<MockData> mock;

    using namespace slv2;
    TrustedStage2State state;

    EXPECT_EQ(Result::Ignore, state.onBlock(mock.context, mock.block, mock.sender1));
    EXPECT_EQ(Result::Ignore, state.onTransactionList(mock.context, mock.tl));
    EXPECT_EQ(Result::Ignore, state.onHash(mock.context, mock.hash, mock.sender1));
    EXPECT_EQ(Result::Ignore, state.onStage2(mock.context, mock.stage2));
    EXPECT_EQ(Result::Ignore, state.onStage3(mock.context, mock.stage3));
    EXPECT_EQ(Result::Ignore, state.onTransaction(mock.context, mock.trans));
    EXPECT_EQ(Result::Finish, state.onRoundTable(mock.context, 1));
}

TEST(TrustedStage2State, TrList_then_all_hashes)
{
    using namespace ::testing;

    NiceMock<MockData> mock;

    using namespace ::testing;
    using namespace slv2;

    TrustedStage2State state;

    EXPECT_NO_THROW(state.on(mock.context));

    // context.enough_stage1() => Ignore || Finish
    struct SequenceBool
    {
        SequenceBool(bool start_from)
            : flag(!start_from)
        {}
        bool flag;
        bool Next()
        {
            flag = !flag;
            return flag;
        }
    };
    SequenceBool seq(false);
    EXPECT_CALL(mock.context, enough_stage1)
        .Times(2) // on(), onHash()-#3
        .WillRepeatedly(Invoke(&seq, &SequenceBool::Next));

    EXPECT_EQ(Result::Ignore, state.onStage1(mock.context, mock.stage1));
    EXPECT_EQ(Result::Finish, state.onStage1(mock.context, mock.stage1));
}
