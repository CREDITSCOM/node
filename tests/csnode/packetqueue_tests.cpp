#include <gtest/gtest.h>
#include <csnode/packetqueue.hpp>

const size_t kMaxPacketTransactions = 100;
const size_t kMaxPacketsPerRound = 10;
const size_t kMaxQueueSize = 100;

class PacketCreator {
public:
    using Pointer = void*;
    using Default = int;

    template<typename T>
    constexpr static auto create() {
        if constexpr (std::is_pointer_v<T>) {
            return new cs::PacketQueue(kMaxQueueSize, kMaxPacketTransactions, kMaxPacketsPerRound);
        }
        else {
            return cs::PacketQueue(kMaxQueueSize, kMaxPacketTransactions, kMaxPacketsPerRound);
        }
    }
};

TEST(PacketQueue, CreationAndDestroy) {
    cs::PacketQueue* queue = PacketCreator::create<PacketCreator::Pointer>();
    delete queue;

    queue = nullptr;

    ASSERT_EQ(queue, nullptr);
}

TEST(PacketQueue, EmptyState) {
    cs::PacketQueue queue = PacketCreator::create<PacketCreator::Default>();
    ASSERT_EQ(queue.isEmpty(), true);
}

void addTransactions(cs::PacketQueue& queue) {
    for (size_t i = 0; i < (kMaxPacketTransactions * 2) + 1; ++i) {
        queue.push(csdb::Transaction{});
    }
}

TEST(PacketQueue, pushTransaction) {
    cs::PacketQueue queue = PacketCreator::create<PacketCreator::Default>();
    addTransactions(queue);

    ASSERT_EQ(queue.size(), 3);
}

TEST(PacketQueue, popTransactionsBlocks) {
    cs::PacketQueue queue = PacketCreator::create<PacketCreator::Default>();
    addTransactions(queue);

    while (!queue.isEmpty()) {
        queue.pop();
    }

    ASSERT_EQ(queue.isEmpty(), true);
}
