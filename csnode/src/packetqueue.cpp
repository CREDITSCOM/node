#include <csnode/packetqueue.hpp>
#include <csnode/conveyer.hpp>

cs::PacketQueue::PacketQueue(size_t queueSize, size_t transactionsSize, size_t packetsPerRound)
: maxQueueSize_(queueSize)
, maxTransactionsSize_(transactionsSize)
, maxPacketsPerRound_(packetsPerRound) {
    cachedRound_ = 0;
    cachedPackets_ = 0;
}

bool cs::PacketQueue::push(const csdb::Transaction& transaction) {
    if (queue_.size() >= maxQueueSize_) {
        return false;
    }

    if (queue_.empty() || queue_.back().transactions().size() >= maxTransactionsSize_) {
        queue_.push_back(cs::TransactionsPacket{});
    }

    return queue_.back().addTransaction(transaction);
}

void cs::PacketQueue::push(const cs::TransactionsPacket& packet) {
    // ignore size of queue for packs
    queue_.push_back(packet);
    queue_.push_back(cs::TransactionsPacket{});
}

std::optional<cs::TransactionsBlock> cs::PacketQueue::pop() {
    const auto round = cs::Conveyer::instance().currentRoundNumber();

    if (round == cachedRound_ && cachedPackets_ >= maxPacketsPerRound_) {
        return std::nullopt;
    }

    if (round != cachedRound_) {
        cachedPackets_ = 0;
    }

    if (queue_.empty()) {
        return std::nullopt;
    }

    cs::TransactionsBlock block;

    while (!queue_.empty() || cachedPackets_ != maxPacketsPerRound_) {
        block.push_back(std::move(queue_.front()));
        queue_.pop_front();

        ++cachedPackets_;
    }

    cachedRound_ = round;
    return std::make_optional(std::move(block));
}

typename std::deque<cs::TransactionsPacket>::const_iterator cs::PacketQueue::begin() const {
    return queue_.begin();
}

typename std::deque<cs::TransactionsPacket>::const_iterator cs::PacketQueue::end() const {
    return queue_.end();
}

size_t cs::PacketQueue::size() const {
    return queue_.size();
}

bool cs::PacketQueue::isEmpty() const {
    return queue_.empty();
}

std::deque<cs::TransactionsPacket>::const_reference cs::PacketQueue::back() const {
    return queue_.back();
}
