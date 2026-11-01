#include <gtest/gtest.h>

#include <net/packet.hpp>

// p2p delivers a peer's payload as is, even an empty one (Transport::OnMessageReceived)
TEST(Packet, EmptyPacketHeaderIsInvalid) {
    Packet packet(cs::Bytes{});
    ASSERT_FALSE(packet.isHeaderValid());
}

TEST(Packet, FlagsOnlyNodePacketHeaderIsInvalid) {
    Packet packet(cs::Bytes{ 0 });
    ASSERT_FALSE(packet.isHeaderValid());
}
