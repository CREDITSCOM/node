#ifndef PACKET_HPP
#define PACKET_HPP

#include <iostream>
#include <memory>
#include <vector>

#include <cscrypto/cscrypto.hpp>
#include <lib/system/common.hpp>
#include <lib/system/hash.hpp>
#include <lib/system/logger.hpp>
#include <lib/system/utils.hpp>

enum BaseFlags : uint8_t {
    Clear = 0,
    NetworkMsg = 1,
    Compressed = 1 << 1,
    Encrypted = 1 << 2,
    Signed = 1 << 3
};

enum MsgTypes : uint8_t {
    RoundTableSS,
    Transactions,
    FirstTransaction,
    NewBlock,
    BlockHash,
    BlockRequest,
    RequestedBlock,
    FirstStage,
    SecondStage,
    ThirdStage,
    FirstStageRequest,
    SecondStageRequest,
    ThirdStageRequest,
    RoundTableRequest,
    RoundTableReply,
    TransactionPacket,
    TransactionsPacketRequest,
    TransactionsPacketReply,
    NewCharacteristic,
    WriterNotification,
    FirstSmartStage,
    SecondSmartStage,
    RoundTable = 22,
    ThirdSmartStage,
    SmartFirstStageRequest,
    SmartSecondStageRequest,
    SmartThirdStageRequest,
    HashReply,
    RejectedContracts,
    RoundPackRequest,
    StateRequest,
    StateReply,
    BigBang = 35,
    EmptyRoundPack,
    NodeStopRequest = 255
};

enum class Offsets : uint32_t {
    BaseFlags = 0,
    MsgTypes = 1,
    RoundNumber = 2,
    HeaderLength = RoundNumber + sizeof(cs::RoundNumber)
};

class Packet {
public:
    static const char* messageTypeToString(MsgTypes messageType);

    explicit Packet(cs::Bytes&& data) : data_(std::move(data)) {}

    Packet() = default;
    Packet(const Packet&) = default;
    Packet(Packet&&) = default;
    Packet& operator=(const Packet&) = default;
    Packet& operator=(Packet&&) = default;

    bool isNetwork() const {
        return checkFlag(BaseFlags::NetworkMsg);
    }

    bool isCompressed() const {
        return checkFlag(BaseFlags::Compressed);
    }

    bool isEncrypted() const {
        return checkFlag(BaseFlags::Encrypted);
    }

    bool isSigned() const {
        return checkFlag(BaseFlags::Signed);
    }

    MsgTypes getType() const {
        return getWithOffset<MsgTypes>(Offsets::MsgTypes);
    }

    cs::RoundNumber getRoundNum() const {
        return getWithOffset<cs::RoundNumber>(Offsets::RoundNumber);
    }

    void* data() {
        return data_.data();
    }

    const void* data() const {
        return data_.data();
    }

    size_t size() const {
        return data_.size();
    }

    void setSize(uint32_t size) {
        data_.resize(size);
    }

    bool isHeaderValid() const;

    const uint8_t* getMsgData() const {
        return static_cast<const uint8_t*>(data_.data()) + getHeadersLength();
    }

    size_t getMsgSize() const {
        return size() > getHeadersLength() ? size() - getHeadersLength() : 0;
    }

    uint32_t getHeadersLength() const {
        return static_cast<uint32_t>(Offsets::HeaderLength);
    }

    explicit operator bool() const {
        return data_.size();
    }

private:
    bool checkFlag(const BaseFlags flag) const {
        return (*static_cast<const uint8_t*>(data_.data()) & flag) != 0;
    }

    template <typename T>
    const T& getWithOffset(const Offsets offset) const {
        return *(reinterpret_cast<const T*>(
                    static_cast<const uint8_t*>(data_.data()) + static_cast<uint32_t>(offset)));
    }

    cs::Bytes data_;
};

using PacketPtr = Packet*;

std::ostream& operator<<(std::ostream& os, const Packet& packet);

#endif  // PACKET_HPP
