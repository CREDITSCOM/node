#include <MockData.h>
#include <states/TrustedStage1State.h>

#include <gtest/gtest.h>

TEST(TrustedStage1State, TrivialMethods)
{
    MockData mock;

    using namespace slv2;
    TrustedStage1State state;

    EXPECT_EQ(Result::Ignore, state.onBlock(mock.context, mock.block, mock.sender1));
    EXPECT_EQ(Result::Ignore, state.onStage1(mock.context, mock.stage1));
    EXPECT_EQ(Result::Ignore, state.onStage2(mock.context, mock.stage2));
    EXPECT_EQ(Result::Ignore, state.onStage3(mock.context, mock.stage3));
    EXPECT_EQ(Result::Ignore, state.onTransaction(mock.context, mock.trans));
    EXPECT_EQ(Result::Finish, state.onRoundTable(mock.context, 1));
}

TEST(TrustedStage1State, TrList_then_all_hashes)
{
    using namespace ::testing;

    NiceMock<MockData> mock;

    EXPECT_CALL(mock.context, is_block_deferred)
        .Times(2) // on(), onHash()-#3
        .WillRepeatedly(Return(true));
    EXPECT_CALL(mock.context, flush_deferred_block)
        .Times(2);

    using namespace slv2;
    TrustedStage1State state;

    EXPECT_NO_THROW(state.on(mock.context));

    EXPECT_EQ(Result::Ignore, state.onTransactionList(mock.context, mock.tl));

    EXPECT_EQ(Result::Ignore, state.onHash(mock.context, mock.hash, mock.sender1));
    EXPECT_EQ(Result::Ignore, state.onHash(mock.context, mock.hash, mock.sender2));
    EXPECT_EQ(Result::Finish, state.onHash(mock.context, mock.hash, mock.sender3));
}

TEST(TrustedStage1State, All_hashes_then_TrList)
{
    using namespace ::testing;

    NiceMock<MockData> mock;

    EXPECT_CALL(mock.context, is_block_deferred)
        .Times(2) // on(), onHash()-#3
        .WillRepeatedly(Return(true));
    EXPECT_CALL(mock.context, flush_deferred_block)
        .Times(2);

    using namespace slv2;
    TrustedStage1State state;

    EXPECT_NO_THROW(state.on(mock.context));

    EXPECT_EQ(Result::Ignore, state.onHash(mock.context, mock.hash, mock.sender1));
    EXPECT_EQ(Result::Ignore, state.onHash(mock.context, mock.hash, mock.sender2));
    EXPECT_EQ(Result::Ignore, state.onHash(mock.context, mock.hash, mock.sender3));

    EXPECT_EQ(Result::Finish, state.onTransactionList(mock.context, mock.tl));
}

TEST(TrustedStage1State, TrList_between_hashes)
{
    using namespace ::testing;

    NiceMock<MockData> mock;

    EXPECT_CALL(mock.context, is_block_deferred)
        .Times(2) // on(), onHash()-#3
        .WillRepeatedly(Return(true));
    EXPECT_CALL(mock.context, flush_deferred_block)
        .Times(2);

    using namespace slv2;
    TrustedStage1State state;

    EXPECT_NO_THROW(state.on(mock.context));

    EXPECT_EQ(Result::Ignore, state.onHash(mock.context, mock.hash, mock.sender1));

    EXPECT_EQ(Result::Ignore, state.onTransactionList(mock.context, mock.tl));

    EXPECT_EQ(Result::Ignore, state.onHash(mock.context, mock.hash, mock.sender2));
    EXPECT_EQ(Result::Finish, state.onHash(mock.context, mock.hash, mock.sender3));
}
