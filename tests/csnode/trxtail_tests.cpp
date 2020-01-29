#define TESTING

#include "gtest/gtest.h"
#include <csnode/transactionstail.hpp>

TEST(TransactionTail, BasicOperations) {

    using TransactionId = int64_t;
    static constexpr size_t BitSize = 1024;
    cs::TransactionsTail tail;

    uint64_t maxId = 0;
    uint64_t cnt = 0;
//    uint64_t lastAllowed = 0;
    uint64_t maxHeap = 0;
    for (uint64_t j = 0; j < 10; ++j) {
        uint64_t deltaPlus = 1000;// std::rand() % 1000;
        maxHeap = cnt;
        for (uint64_t i = 1; i < deltaPlus; ++i) {
            //if (std::rand() < 1'500'000'000) {
            ASSERT_FALSE(tail.isDuplicated(i + maxHeap));
            if (tail.isAllowed(i + maxHeap)) {
                tail.push(i + maxHeap);
                maxId = i;
                ++cnt;
            }


           // }
            //else {
            //    if (maxHeap > 0 && maxHeap < maxId) {
            //        lastAllowed = maxHeap;
            //    }
            //    maxHeap = i;
            //}
        }
        //uint64_t deltaMinus = std::rand() % 1000;
        //if (deltaMinus < cnt) {
            for (uint64_t i = 1; i < 900; ++i) {
                tail.erase(maxId);
                maxId = tail.getLastTransactionId();
                --cnt;
            }
        //}

    }
    tail.push(1022);
    tail.push(1023);
    tail.push(1024);
    tail.push(1025);
    ASSERT_FALSE(tail.isAllowed(1024));
    ASSERT_FALSE(tail.isAllowed(1025));
    ASSERT_TRUE(tail.isAllowed(1026));
    tail.erase(1024);
    tail.erase(1025);
    ASSERT_TRUE(tail.isAllowed(1024));
    ASSERT_TRUE(tail.isAllowed(1025));


    //ASSERT_TRUE(cnt > 0);
    //ASSERT_FALSE(tail.empty());
    //ASSERT_TRUE(tail.getLastTransactionId() == maxId);
    //if(lastAllowed > 0) ASSERT_TRUE(tail.isAllowed(lastAllowed));
    //ASSERT_TRUE(lastAllowed > 0);


 /*   ASSERT_FALSE(heap.contains(37));
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
    ASSERT_TRUE(heap.contains(2));
    ASSERT_EQ(heap.count(), 4);*/

}
