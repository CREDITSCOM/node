#include <gtest/gtest.h>
#include <csnode/packetqueue.hpp>

const size_t kMaxPacketTransactions = 100;
const size_t kMaxPacketsPerRound = 10;
const size_t kMaxQueueSize = 1000000;

TEST(PacketQueue, CreationAndDestroy) {
    cs::PacketQueue* queue = new cs::PacketQueue(kMaxQueueSize, kMaxPacketTransactions, kMaxPacketsPerRound);
    delete queue;

    queue = nullptr;

    ASSERT_EQ(queue, nullptr);
}
