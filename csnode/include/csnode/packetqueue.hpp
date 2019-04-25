#ifndef PACKETQUEUE_HPP
#define PACKETQUEUE_HPP

#include <deque>
#include <optional>

#include <csnode/nodecore.hpp>
#include <boost/noncopyable.hpp>

namespace cs {
// implements business logic for transpaction packet
class PacketQueue : public boost::noncopyable {
public:
    explicit PacketQueue(size_t queueSize, size_t transactionsSize, size_t packetsPerRound);
    ~PacketQueue() = default;

    bool push(const csdb::Transaction& transaction);
    void push(const cs::TransactionsPacket& packet);

    cs::TransactionsBlock pop();

    std::deque<cs::TransactionsPacket>::const_iterator begin() const;
    std::deque<cs::TransactionsPacket>::const_iterator end() const;

    size_t size() const;
    bool isEmpty() const;
    std::deque<cs::TransactionsPacket>::const_reference back() const;

private:
    std::deque<cs::TransactionsPacket> queue_;

    size_t maxQueueSize_;
    size_t maxTransactionsSize_;
    size_t maxPacketsPerRound_;

    cs::RoundNumber cachedRound_;
    size_t cachedPackets_;
};
}

#endif // PACKETQUEUE_HPP
