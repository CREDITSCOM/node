#ifndef PACKETS_QUEUE_HPP
#define PACKETS_QUEUE_HPP

#include <list>
#include <utility>

#include <lib/system/common.hpp>

#include "packet.hpp"

class PacketsQueue {
public:
    using SenderAndPacket = std::pair<cs::PublicKey, Packet>;

    bool empty() const;
    SenderAndPacket pop();
    void push(const cs::PublicKey&, Packet&&);
    void clear();
private:
    enum class Priority {
        kFirst,
        kSecond
    };

    Priority getPriority(MsgTypes type) const;
    size_t numPackets() const;
    size_t numBytes() const;
    bool shrink();

    constexpr static size_t kMaxPacketsToHandle = 1ul << 17; // 131_072
    constexpr static size_t kMaxBytesToHandle = 1ul << 29; // 536_870_912 bytes

    size_t firstQBytes_ = 0;
    size_t secondQBytes_ = 0;

    std::list<SenderAndPacket> firstPriorityQ_;
    std::list<SenderAndPacket> secondPriorityQ_;
};

#endif // PACKETS_QUEUE_HPP
