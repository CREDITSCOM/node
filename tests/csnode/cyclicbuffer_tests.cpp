
//#define TESTING

#include <gtest/gtest.h>
#include "cyclicbuffer.hpp"

TEST(CyclicBuffer, EmptyAfterCreation) {
  CyclicBuffer<int, 10> buffer;
  ASSERT_TRUE(buffer.empty());
}

TEST(CyclicBuffer, NotEmptyAfterElementAddedToBack) {
  CyclicBuffer<int, 10> buffer;
  buffer.push_back(1);
  ASSERT_FALSE(buffer.empty());
}

TEST(CyclicBuffer, NotEmptyAfterElementAddedToFront) {
  CyclicBuffer<int, 10> buffer;
  buffer.push_front(1);
  ASSERT_FALSE(buffer.empty());
}

TEST(CyclicBuffer, EmptyAfterElementAddedToBackAndPoppedFromBack) {
  CyclicBuffer<int, 10> buffer;
  buffer.push_back(1);
  buffer.pop_back();
  ASSERT_TRUE(buffer.empty());
}

TEST(CyclicBuffer, EmptyAfterElementAddedToFrontAndPoppedFromFront) {
  CyclicBuffer<int, 10> buffer;
  buffer.push_front(1);
  buffer.pop_front();
  ASSERT_TRUE(buffer.empty());
}

TEST(CyclicBuffer, EmptyAfterElementAddedToFrontAndPoppedFromBack) {
  CyclicBuffer<int, 10> buffer;
  buffer.push_front(1);
  buffer.pop_back();
  ASSERT_TRUE(buffer.empty());
}

TEST(CyclicBuffer, EmptyAfterElementAddedToBackAndPoppedFromFront) {
  CyclicBuffer<int, 10> buffer;
  buffer.push_back(1);
  buffer.pop_front();
  ASSERT_TRUE(buffer.empty());
}

TEST(CyclicBuffer, NotEmptyAfterTwoElementsAddedToFrontAndOnePoppedFromFront) {
  CyclicBuffer<int, 10> buffer;
  buffer.push_front(1);
  buffer.push_front(2);
  buffer.pop_front();
  ASSERT_FALSE(buffer.empty());
}

TEST(CyclicBuffer, NotEmptyAfterTwoElementsAddedToFrontAndOnePoppedFromBack) {
  CyclicBuffer<int, 10> buffer;
  buffer.push_front(1);
  buffer.push_front(2);
  buffer.pop_back();
  ASSERT_FALSE(buffer.empty());
}

TEST(CyclicBuffer, NotEmptyAfterTwoElementsAddedToBackAndOnePoppedFromFront) {
  CyclicBuffer<int, 10> buffer;
  buffer.push_back(1);
  buffer.push_back(2);
  buffer.pop_front();
  ASSERT_FALSE(buffer.empty());
}

TEST(CyclicBuffer, NotEmptyAfterTwoElementsAddedToBackAndOnePoppedFromBack) {
  CyclicBuffer<int, 10> buffer;
  buffer.push_back(1);
  buffer.push_back(2);
  buffer.pop_back();
  ASSERT_FALSE(buffer.empty());
}

TEST(CyclicBuffer, PeekedFromBackIsEqualToPushedToBack) {
  CyclicBuffer<int, 10> buffer;
  buffer.push_back(1234);
  ASSERT_EQ(1234, buffer.back());
}

TEST(CyclicBuffer, PeekedFromBackIsEqualToPushedToFront) {
  CyclicBuffer<int, 10> buffer;
  buffer.push_front(1234);
  ASSERT_EQ(1234, buffer.back());
}

TEST(CyclicBuffer, PeekedFromFrontIsEqualToPushedToFront) {
  CyclicBuffer<int, 10> buffer;
  buffer.push_front(1234);
  ASSERT_EQ(1234, buffer.front());
}

TEST(CyclicBuffer, PeekedFromFrontIsEqualToPushedToBack) {
  CyclicBuffer<int, 10> buffer;
  buffer.push_back(1234);
  ASSERT_EQ(1234, buffer.front());
}

TEST(CyclicBuffer, SizeIsZeroAfterCreation) {
  CyclicBuffer<int, 10> buffer;
  ASSERT_EQ(buffer.size(), 0);
}

TEST(CyclicBuffer, SizeIsEqualToNumberOfPushedToBack) {
  CyclicBuffer<int, 10> buffer;
  buffer.push_back(1);
  buffer.push_back(2);
  buffer.push_back(3);
  buffer.push_back(4);
  buffer.push_back(5);
  ASSERT_EQ(buffer.size(), 5);
}

TEST(CyclicBuffer, SizeIsEqualToNumberOfPushedToFront) {
  CyclicBuffer<int, 10> buffer;
  buffer.push_front(1);
  buffer.push_front(2);
  buffer.push_front(3);
  ASSERT_EQ(buffer.size(), 3);
}

TEST(CyclicBuffer, SizeIsCorrectAfterPushingToBackAndPoppngFromBack) {
  CyclicBuffer<int, 10> buffer;
  buffer.push_back(1);
  buffer.push_back(2);
  buffer.pop_back();
  ASSERT_EQ(buffer.size(), 1);
}

TEST(CyclicBuffer, SizeIsCorrectAfterPushingToBackAndPoppngFromFront) {
  CyclicBuffer<int, 10> buffer;
  buffer.push_back(1);
  buffer.push_back(2);
  buffer.pop_front();
  ASSERT_EQ(buffer.size(), 1);
}

TEST(CyclicBuffer, SizeIsCorrectAfterPushingToFrontAndPoppngFromFront) {
  CyclicBuffer<int, 10> buffer;
  buffer.push_front(1);
  buffer.push_front(2);
  buffer.pop_front();
  ASSERT_EQ(buffer.size(), 1);
}

TEST(CyclicBuffer, SizeIsCorrectAfterPushingToFrontAndPoppngFromBack) {
  CyclicBuffer<int, 10> buffer;
  buffer.push_front(1);
  buffer.push_front(2);
  buffer.pop_back();
  ASSERT_EQ(buffer.size(), 1);
}

TEST(CyclicBuffer, ElementsAreCorrectlyAccessibleByIndexAfterPushedToBack) {
  CyclicBuffer<int, 20> buffer;
  buffer.push_back(1);
  buffer.push_back(2);
  buffer.push_back(3);
  buffer.push_back(7);
  buffer.push_back(125);
  ASSERT_EQ(1, buffer[0]);
  ASSERT_EQ(2, buffer[1]);
  ASSERT_EQ(3, buffer[2]);
  ASSERT_EQ(7, buffer[3]);
  ASSERT_EQ(125, buffer[4]);
}

TEST(CyclicBuffer, ElementsAreCorrectlyAccessibleByIndexAfterPushedToFront) {
  CyclicBuffer<int, 20> buffer;
  buffer.push_front(6);
  buffer.push_front(7);
  buffer.push_front(8);
  buffer.push_front(22);
  buffer.push_front(324);
  ASSERT_EQ(6, buffer[4]);
  ASSERT_EQ(7, buffer[3]);
  ASSERT_EQ(8, buffer[2]);
  ASSERT_EQ(22, buffer[1]);
  ASSERT_EQ(324, buffer[0]);
}

TEST(CyclicBuffer, BufferIsNotFullAfterCreation) {
  CyclicBuffer<int, 10> buffer;
  ASSERT_FALSE(buffer.full());
}

TEST(CyclicBuffer, BufferIsFullAfterMaxElementsWerePushedToBack) {
  CyclicBuffer<int, 6> buffer;
  buffer.push_back(1);
  buffer.push_back(2);
  buffer.push_back(3);
  buffer.push_back(4);
  buffer.push_back(5);
  buffer.push_back(6);
  ASSERT_TRUE(buffer.full());
}

TEST(CyclicBuffer, BufferIsFullAfterMaxElementsWerePushedToFront) {
  CyclicBuffer<int, 4> buffer;
  buffer.push_front(1);
  buffer.push_front(2);
  buffer.push_front(3);
  buffer.push_front(4);
  ASSERT_TRUE(buffer.full());
}

TEST(CyclicBuffer, BufferIsFullAfterMaxElementsWerePushedToBackAndFront) {
  CyclicBuffer<int, 4> buffer;
  buffer.push_back(1);
  buffer.push_back(2);
  buffer.push_front(3);
  buffer.push_front(4);
  ASSERT_TRUE(buffer.full());
}

TEST(CyclicBuffer, BufferIsFullAfterMaxElementsWerePushedToFrontAndBack) {
  CyclicBuffer<int, 4> buffer;
  buffer.push_front(1);
  buffer.push_front(2);
  buffer.push_back(3);
  buffer.push_back(4);
  ASSERT_TRUE(buffer.full());
}

TEST(CyclicBuffer,
     BufferIsNotFullAfterMaxElementsWerePushedToBackAndThenOnePoppedFromBack) {
  CyclicBuffer<int, 4> buffer;
  buffer.push_back(1);
  buffer.push_back(2);
  buffer.push_back(3);
  buffer.push_back(4);
  buffer.pop_back();
  ASSERT_FALSE(buffer.full());
}

TEST(CyclicBuffer,
     BufferIsNotFullAfterMaxElementsWerePushedToBackAndThenOnePoppedFromFront) {
  CyclicBuffer<int, 4> buffer;
  buffer.push_back(1);
  buffer.push_back(2);
  buffer.push_back(3);
  buffer.push_back(4);
  buffer.pop_front();
  ASSERT_FALSE(buffer.full());
}

TEST(CyclicBuffer,
     BufferIsNotFullAfterMaxElementsWerePushedToFrontAndThenOnePoppedFromBack) {
  CyclicBuffer<int, 4> buffer;
  buffer.push_front(1);
  buffer.push_front(2);
  buffer.push_front(3);
  buffer.push_front(4);
  buffer.pop_back();
  ASSERT_FALSE(buffer.full());
}

TEST(
    CyclicBuffer,
    BufferIsNotFullAfterMaxElementsWerePushedToFrontAndThenOnePoppedFromFront) {
  CyclicBuffer<int, 4> buffer;
  buffer.push_front(1);
  buffer.push_front(2);
  buffer.push_front(3);
  buffer.push_front(4);
  buffer.pop_front();
  ASSERT_FALSE(buffer.full());
}
