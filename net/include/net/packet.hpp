#ifndef PACKET_HPP
#define PACKET_HPP

#include <boost/asio.hpp>

#include <cscrypto/cscrypto.hpp>
#include <lib/system/allocators.hpp>
#include <lib/system/common.hpp>
#include <lib/system/hash.hpp>
#include <lib/system/logger.hpp>
#include "lib/system/utils.hpp"

#include <lz4.h>

#include <iostream>
#include <memory>
#include <vector>

const uint32_t MaxRememberPackets = 100000;

/*
    Static min memory usage (see types below):

    1 fragment = 1'024 b
    1 message = 80 b * 4'096 fragments = 327'680 b
    1 collector = 2'048 messages * 327'680 b = 671'088'640 b
*/

namespace ip = boost::asio::ip;

enum BaseFlags : uint8_t {
    NetworkMsg = 1,
    Fragmented = 1 << 1,
    Broadcast = 1 << 2,  // send packet to Neighbours, Neighbours can resend it to others
    Compressed = 1 << 3,
    Encrypted = 1 << 4,
    Signed = 1 << 5,
    Direct = 1 << 6,  // send packet to Direct only, Node _cant_ resend it
};

enum Offsets : uint32_t {
    FragmentId = 1,
    FragmentsNum = 3,
    IdWhenFragmented = 5,
    IdWhenSingle = 1,
    SenderWhenFragmented = 13,
    SenderWhenSingle = 9,
    AddresseeWhenFragmented = cscrypto::kPublicKeySize + SenderWhenFragmented,
    AddresseeWhenSingle = cscrypto::kPublicKeySize + SenderWhenSingle
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

class Packet {
public:
    static const uint32_t MaxSize = 1024;
    static const uint32_t MaxFragments = 4096;

    static const uint32_t SmartRedirectTreshold = 10000;

    static const char* messageTypeToString(MsgTypes messageType);

    Packet() = default;
    explicit Packet(cs::Bytes&& data)
    : region_(std::move(data)) {
    }

    Packet(const Packet&) = default;
    Packet& operator=(const Packet&) = default;

    bool isNetwork() const {
        return checkFlag(BaseFlags::NetworkMsg);
    }

    bool isFragmented() const {
        return checkFlag(BaseFlags::Fragmented);
    }

    bool isBroadcast() const {
        return checkFlag(BaseFlags::Broadcast);
    }

    bool isCompressed() const {
        return checkFlag(BaseFlags::Compressed);
    }

    bool isDirect() const {
        return checkFlag(BaseFlags::Direct);
    }

    const cs::Hash& getHash() const {
        if (!hashed_) {
            hash_ = generateHash(region_.data(), region_.size());
            hashed_ = true;
        }

        return hash_;
    }

    bool addressedToMe(const cs::PublicKey& myKey) const {
        return isNetwork() || isDirect() || (isBroadcast() && !(getSender() == myKey)) || getAddressee() == myKey;
    }

    const cs::PublicKey& getSender() const {
        return getWithOffset<cs::PublicKey>(isFragmented() ? Offsets::SenderWhenFragmented : Offsets::SenderWhenSingle);
    }

    const cs::PublicKey& getAddressee() const {
        return getWithOffset<cs::PublicKey>(isFragmented() ? Offsets::AddresseeWhenFragmented : Offsets::AddresseeWhenSingle);
    }

    const uint64_t& getId() const {
        return getWithOffset<uint64_t>(isFragmented() ? Offsets::IdWhenFragmented : Offsets::IdWhenSingle);
    }

    const cs::Hash& getHeaderHash() const;
    bool isHeaderValid() const;

    const uint16_t& getFragmentId() const {
        return getWithOffset<uint16_t>(Offsets::FragmentId);
    }

    const uint16_t& getFragmentsNum() const {
        return getWithOffset<uint16_t>(Offsets::FragmentsNum);
    }

    MsgTypes getType() const {
        return getWithOffset<MsgTypes>(getHeadersLength());
    }

    cs::RoundNumber getRoundNum() const {
        return getWithOffset<cs::RoundNumber>(getHeadersLength() + 1);
    }

    void* data() {
        return region_.data();
    }

    const void* data() const {
        return region_.data();
    }

    size_t size() const {
        return region_.size();
    }

    void setSize(uint32_t size) {
        region_.resize(size);
    }

    const uint8_t* getMsgData() const {
        return static_cast<const uint8_t*>(region_.data()) + getHeadersLength();
    }

    size_t getMsgSize() const {
        return size() - getHeadersLength();
    }

    uint32_t getHeadersLength() const;
    void recalculateHeadersLength();

    explicit operator bool() const {
        return region_.size();
    }

    boost::asio::mutable_buffer encode(boost::asio::mutable_buffer tempBuffer) {
        if (region_.size() == 0) {
            cswarning() << "Encoding empty packet";
            return boost::asio::buffer(tempBuffer.data(), 0);
        }

        if (isCompressed()) {
            static_assert(sizeof(BaseFlags) == sizeof(char), "BaseFlags should be char sized");
            const size_t headerSize = getHeadersLength();

            // Packet::MaxSize is a part of implementation magic(
            assert(tempBuffer.size() == Packet::MaxSize);

            char* source = reinterpret_cast<char*>(region_.data());
            char* dest = static_cast<char*>(tempBuffer.data());

            // copy header
            std::copy(source, source + headerSize, dest);

            int sourceSize = static_cast<int>(region_.size() - headerSize);
            int destSize = static_cast<int>(tempBuffer.size() - headerSize);

            int compressedSize = LZ4_compress_default(source + headerSize, dest + headerSize, sourceSize, destSize);

            if ((compressedSize > 0) && (compressedSize < sourceSize)) {
                return boost::asio::buffer(dest, static_cast<size_t>(compressedSize) + headerSize);
            }
            else {
                csdetails() << "Skipping packet compression, rawSize = " << sourceSize << ", compressedSize = " << compressedSize;
                *source &= ~BaseFlags::Compressed;
            }
        }

        char* source = reinterpret_cast<char*>(region_.data());
        char* dest = static_cast<char*>(tempBuffer.data());

        std::copy(source, source + region_.size(), dest);
        return boost::asio::buffer(dest, region_.size());
    }

    size_t decode(size_t packetSize = 0) {
        if (packetSize == 0) {
            return 0;
        }

        if (isCompressed()) {
            static_assert(sizeof(BaseFlags) == sizeof(char), "BaseFlags should be char sized");
            const size_t headerSize = getHeadersLength();

            assert(headerSize <= packetSize);

            if (headerSize > packetSize) {
                cserror() << "Malformed compressed packet detected";
                return 0;
            }
            if (headerSize == packetSize) {
                cserror() << "Data is empty in compressed packet";
                return 0;
            }

            // It's a part of implementation magic(
            // eg. <IPackMan> allocates Packet::MaxSize packet implicitly
            assert(region_.size() == Packet::MaxSize);

            char* source = reinterpret_cast<char*>(region_.data());
            char dest[Packet::MaxSize];

            int sourceSize = static_cast<int>(packetSize - headerSize);
            int destSize = static_cast<int>(sizeof(dest) - headerSize);

            auto uncompressedSize = LZ4_decompress_safe(source + headerSize, dest, sourceSize, destSize);

            if ((uncompressedSize > 0) && (uncompressedSize <= destSize)) {
                std::copy(dest, dest + uncompressedSize, source + headerSize);
                *source &= ~BaseFlags::Compressed;
                packetSize = static_cast<size_t>(uncompressedSize) + headerSize;
            }
            else {
                cserror() << "Decoding malformed packet content";
                return 0;
            }
        }

        return packetSize;
    }

    // returns true if is not fragmented or has valid fragmentation data
    bool hasValidFragmentation() const {
        if (isFragmented()) {
            const auto fragment = getFragmentId();
            const auto count = getFragmentsNum();

            if (count == 0 || fragment >= count) {
                return false;
            }
        }

        return true;
    }

private:
    bool checkFlag(const BaseFlags flag) const {
        return (*static_cast<const uint8_t*>(region_.data()) & flag) != 0;
    }

    uint32_t calculateHeadersLength() const;

    template <typename T>
    const T& getWithOffset(const uint32_t offset) const {
        return *(reinterpret_cast<const T*>(static_cast<const uint8_t*>(region_.data()) + offset));
    }

private:
    cs::Bytes region_;
    friend class Network;

    mutable bool hashed_ = false;
    mutable cs::Hash hash_;

    mutable bool headerHashed_ = false;
    mutable cs::Hash headerHash_;

    mutable uint32_t headersLength_ = 0;

    friend class IPacMan;
};

using PacketPtr = Packet*;

std::ostream& operator<<(std::ostream& os, const Packet& packet);

#endif  // PACKET_HPP
