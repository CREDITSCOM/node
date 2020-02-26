#include "packetsqueue.hpp"

#include <stdexcept>

bool PacketsQueue::empty() const {
    return firstPriorityQ_.empty() && secondPriorityQ_.empty();
}

PacketsQueue::SenderAndPacket PacketsQueue::pop() {
    SenderAndPacket result;

    if (!firstPriorityQ_.empty()) {
        result.first = firstPriorityQ_.front().first;
        result.second = std::move(firstPriorityQ_.front().second);
        firstPriorityQ_.pop_front();
        firstQBytes_ -= result.second.size();
        return result;
    }

    if (!secondPriorityQ_.empty()) {
        result.first = secondPriorityQ_.front().first;
        result.second = std::move(secondPriorityQ_.front().second);
        secondPriorityQ_.pop_front();
        secondQBytes_ -= result.second.size();
        return result;
    }

    throw std::domain_error("PacketsQueue is empty.");
}

void PacketsQueue::push(const cs::PublicKey& sender, Packet&& pack) {
    auto priority = getPriority(pack.getType());

    if (!shrink()) {
        if (priority == Priority::kSecond) return;
        firstPriorityQ_.clear();
        firstQBytes_ = 0;
    }

    if (priority == Priority::kFirst) {
        firstQBytes_ += pack.size();
        firstPriorityQ_.push_back({sender, std::move(pack)});
    }
    else {
        secondQBytes_ += pack.size();
        secondPriorityQ_.push_back({sender, std::move(pack)});
    }
}

PacketsQueue::Priority PacketsQueue::getPriority(MsgTypes type) const {
    switch (type) {
        case MsgTypes::ThirdSmartStage:
        case MsgTypes::Utility:
        case MsgTypes::NodeStopRequest:
        case MsgTypes::RoundTable:
        case MsgTypes::BootstrapTable:
        case MsgTypes::BlockHash:
        case MsgTypes::TransactionPacket:
        case MsgTypes::TransactionsPacketReply:
        case MsgTypes::TransactionsPacketRequest:
        case MsgTypes::FirstStage:
        case MsgTypes::SecondStage:
        case MsgTypes::FirstStageRequest:
        case MsgTypes::SecondStageRequest:
        case MsgTypes::ThirdStageRequest:
        case MsgTypes::ThirdStage:
        case MsgTypes::FirstSmartStage:
        case MsgTypes::SecondSmartStage:
        case MsgTypes::SmartFirstStageRequest:
        case MsgTypes::SmartSecondStageRequest:
        case MsgTypes::SmartThirdStageRequest:
        case MsgTypes::RejectedContracts:
        case MsgTypes::StateReply:
        case MsgTypes::BlockAlarm:
            return Priority::kFirst;
        default:
            return Priority::kSecond;
    }
}

size_t PacketsQueue::numPackets() const {
    return firstPriorityQ_.size() + secondPriorityQ_.size();
}

size_t PacketsQueue::numBytes() const {
    return firstQBytes_ + secondQBytes_;
}

bool PacketsQueue::shrink() {
  if ((numPackets() > kMaxPacketsToHandle) || (numBytes() > kMaxBytesToHandle)) {
    secondPriorityQ_.clear();
    secondQBytes_ = 0;
  }

  return (numPackets() <= kMaxPacketsToHandle) && (numBytes() <= kMaxBytesToHandle);
}
