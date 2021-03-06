#include "gtest/gtest.h"
#include <csnode/bitheap.hpp>

TEST(BeatHeap, BasicOperations) {

    using TransactionId = int64_t;
    static constexpr size_t BitSize = 1024;
    using Heap = cs::BitHeap<TransactionId, BitSize>;
    Heap heap;

    ASSERT_FALSE(heap.contains(37));
    ASSERT_EQ(heap.count(), 0);
    ASSERT_TRUE(heap.empty());

    heap.push(37);

    ASSERT_TRUE(heap.contains(37));
    ASSERT_NE(heap.count(), 0);
    ASSERT_FALSE(heap.empty());
    ASSERT_FALSE(heap.contains(2));

    heap.pop(37);

    ASSERT_FALSE(heap.contains(37));
    
    ASSERT_EQ(heap.count(), 0);
    ASSERT_TRUE(heap.empty());

    heap.push(137);
    heap.push(1037);

    ASSERT_FALSE(heap.contains(37));
    ASSERT_TRUE(heap.contains(137));
    ASSERT_TRUE(heap.contains(1037));
    ASSERT_EQ(heap.count(), 2);
    ASSERT_FALSE(heap.empty());

    heap.pop(1037);

    ASSERT_FALSE(heap.contains(37));
    ASSERT_TRUE(heap.contains(137));
    ASSERT_FALSE(heap.contains(1037));
    ASSERT_TRUE(heap.getGreatest() == 137);
    ASSERT_EQ(heap.count(), 1);
    ASSERT_FALSE(heap.empty());

    heap.push(1000);
    heap.push(1001);
    heap.push(1002);
    heap.push(1004);
    heap.push(1005);
    ASSERT_TRUE(heap.getGreatest() == 1005);
    heap.pop(1004);
    heap.pop(1005);
    ASSERT_TRUE(heap.getGreatest() == 1002);
    ASSERT_FALSE(heap.contains(2));
    ASSERT_EQ(heap.count(), 4);

}
