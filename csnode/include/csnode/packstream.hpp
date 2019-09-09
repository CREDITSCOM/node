#ifndef PACKSTREAM_HPP
#define PACKSTREAM_HPP

#include <algorithm>
#include <cstring>
#include <type_traits>

#include <csnode/nodecore.hpp>
#include <csnode/transactionspacket.hpp>

#include <csdb/pool.hpp>
#include <csdb/transaction.hpp>

#include <lib/system/hash.hpp>

#include <net/packet.hpp>

namespace cs {
class IPackStream {
public:
    void init(const cs::Byte* ptr, const size_t size) {
        ptr_ = ptr;
        end_ = ptr_ + size;
        good_ = true;
    }

    template <typename T>
    bool canPeek() const {
        return static_cast<uint32_t>(end_ - ptr_) >= sizeof(T);
    }

    template <typename T>
    const T& peek() const {
        return *(reinterpret_cast<const T*>(ptr_));
    }

    template <typename T>
    void skip() {
        ptr_ += sizeof(T);
    }

    template <typename T>
    void safeSkip(uint32_t num = 1) {
        auto size = sizeof(T) * num;

        if (!isBytesAvailable(size)) {
            good_ = false;
        }
        else {
            ptr_ += size;
        }
    }

    template <typename T>
    IPackStream& operator>>(T& cont) {
        if (!canPeek<T>()) {
            good_ = false;
        }
        else {
            cont = peek<T>();
            skip<T>();
        }

        return *this;
    }

    template <size_t Length>
    IPackStream& operator>>(FixedString<Length>& str) {
        if (!isBytesAvailable(Length)) {
            good_ = false;
        }
        else {
            std::copy(ptr_, ptr_ + Length, str.data());
            ptr_ += Length;
        }

        return *this;
    }

    template <size_t Length>
    IPackStream& operator>>(cs::ByteArray<Length>& byteArray) {
        if (!isBytesAvailable(Length)) {
            good_ = false;
        }
        else {
            std::copy(ptr_, ptr_ + Length, byteArray.data());
            ptr_ += Length;
        }

        return *this;
    }

    template <typename T, typename A>
    IPackStream& operator>>(std::vector<T, A>& vector) {
        std::size_t size = 0;
        (*this) >> size;

        if (size == 0) {
            return *this;
        }

        // check min needed bytes. It may be more bytes needed on the elements.
        if (!isBytesAvailable(size)) {
            good_ = false;
            return *this;
        }

        std::vector<T, A> entity;
        entity.reserve(size);

        for (std::size_t i = 0; i < size; ++i) {
            T element;
            (*this) >> element;

            if (!good()) {
                break;
            }

            entity.push_back(std::move(element));
        }

        if (entity.size() != size) {
            cserror() << "Pack stream -> vector parsing failed";
            return *this;
        }

        vector = std::move(entity);
        return *this;
    }

    template <typename T, typename U>
    IPackStream& operator>>(std::pair<T, U>& pair) {
        T first;
        (*this) >> first;

        if (!good_) {
            return *this;
        }

        U second;
        (*this) >> second;

        if (!good_) {
            return *this;
        }

        pair = std::make_pair(std::move(first), std::move(second));
        return *this;
    }

    bool good() const {
        return good_;
    }

    bool end() const {
        return ptr_ == end_;
    }

    operator bool() const {
        return good() && !end();
    }

    const cs::Byte* getCurrentPtr() const {
        return ptr_;
    }

    const cs::Byte* getEndPtr() const {
        return end_;
    }

    size_t remainsBytes() const {
        return static_cast<size_t>(end_ - ptr_);
    }

    bool isBytesAvailable(size_t bytes) const {
        return remainsBytes() >= bytes;
    }

private:
    const cs::Byte* ptr_ = nullptr;
    const cs::Byte* end_ = nullptr;
    bool good_ = false;
};

class OPackStream {
public:
    OPackStream(RegionAllocator* allocator, const cs::PublicKey& nodeIdKey)
    : allocator_(allocator)
    , packets_(new Packet[200000/*Packet::MaxFragments*/]())
    , packetsEnd_(packets_)
    , senderKey_(nodeIdKey) {
    }

    ~OPackStream() {
        delete[] packets_;
    }

    void init(cs::Byte flags) {
        clear();
        ++id_;

        newPack();

        *ptr_ = flags;
        ++ptr_;

        if (flags & BaseFlags::Fragmented) {
            *this << static_cast<uint16_t>(0) << packetsCount_;
        }

        if (!(flags & BaseFlags::NetworkMsg)) {
            *this << id_ << senderKey_;
        }
    }

    void init(uint8_t flags, const cs::PublicKey& receiver) {
        init(flags);
        *this << receiver;
    }

    void clear() {
        for (auto ptr = packets_; ptr != packetsEnd_; ++ptr) {
            ptr->~Packet();
        }

        packetsCount_ = 0;
        finished_ = false;
        packetsEnd_ = packets_;
    }

    template <typename T>
    OPackStream& operator<<(const T& value) {
        static_assert(sizeof(T) <= Packet::MaxSize, "Type too long");
        const auto left = static_cast<uint32_t>(end_ - ptr_);

        if (left >= sizeof(T)) {
            *(reinterpret_cast<T*>(ptr_)) = value;
            ptr_ += sizeof(T);
        }
        else {
            const auto pointer = reinterpret_cast<const cs::Byte*>(&value);
            std::copy(pointer, (pointer + left), ptr_);
            newPack();
            std::copy(pointer + left, pointer + sizeof(T), ptr_);
            ptr_ += sizeof(T) - left;
        }

        return *this;
    }

    template <size_t Length>
    OPackStream& operator<<(const FixedString<Length>& str) {
        insertBytes(str.data(), Length);
        return *this;
    }

    template <size_t Length>
    OPackStream& operator<<(const cs::ByteArray<Length>& byteArray) {
        insertBytes(byteArray.data(), Length);
        return *this;
    }

    OPackStream& operator<<(cs::BytesView view) {
        (*this) << view.size();
        insertBytes(view.data(), static_cast<uint32_t>(view.size()));
        return *this;
    }

    template <typename T, typename A>
    OPackStream& operator<<(const std::vector<T, A>& vector) {
        (*this) << vector.size();

        for (const auto& element : vector) {
            (*this) << element;
        }

        return *this;
    }

    template <typename T, typename U>
    OPackStream& operator<<(const std::pair<T, U>& pair) {
        (*this) << pair.first;
        (*this) << pair.second;

        return *this;
    }

    Packet* getPackets() {
        if (!finished_) {
            (packetsEnd_ - 1)->setSize(static_cast<uint32_t>(ptr_ - static_cast<cs::Byte*>((packetsEnd_ - 1)->data())));

            if (packetsCount_ > 1) {
                for (auto p = packets_; p != packetsEnd_; ++p) {
                    cs::Byte* data = static_cast<cs::Byte*>(p->data());

                    // TODO: make next impossible, see newPack()
                    if (!p->isFragmented()) {
                        cserror() << "Malformed packet: Fragmented flag not set for fragmented packet";
                        assert(false);
                    }

                    *reinterpret_cast<uint16_t*>(data + Offsets::FragmentsNum) = packetsCount_;
                }
            }
            finished_ = true;
        }

        return packets_;
    }

    uint32_t getPacketsCount() {
        return packetsCount_;
    }

    cs::Byte* getCurrentPtr() {
        return ptr_;
    }

    uint32_t getCurrentSize() const {
        return static_cast<uint32_t>(ptr_ - static_cast<cs::Byte*>((packetsEnd_ - 1)->data()));
    }

private:
    void newPack() {
        RegionPtr tempBuffer;
        cs::Byte* tail = nullptr;
        static constexpr size_t insertedSize = sizeof(uint16_t) + sizeof(packetsCount_);

        if (packetsCount_ == 1) {
            ptr_ = static_cast<cs::Byte*>(packets_->data());

            if (!packets_->isFragmented()) {
                csdebug() << "Fragmentation flag is not set in fragmented packet, correcting";
                *ptr_ |= BaseFlags::Fragmented;

                packets_->recalculateHeadersLength();

                // insert size_inserted bytes from [1] and shift current content "rightward"
                ++ptr_;

                size_t shiftedSize = static_cast<size_t>(end_ - ptr_);
                tempBuffer = allocator_->allocateNext(static_cast<uint32_t>(shiftedSize));
                tail = static_cast<cs::Byte*>(tempBuffer->data());

                std::copy(ptr_, end_, tail);
                *this << static_cast<uint16_t>(0) << static_cast<decltype(packetsCount_)>(0);

                insertBytes(tail, static_cast<uint32_t>(shiftedSize - insertedSize));
                tail += (shiftedSize - insertedSize);
            }
        }

        new (packetsEnd_) Packet(allocator_->allocateNext(Packet::MaxSize));

        ptr_ = static_cast<cs::Byte*>(packetsEnd_->data());
        end_ = ptr_ + packetsEnd_->size();

        if (packetsEnd_ != packets_) {
            auto begin = static_cast<cs::Byte*>(packets_->data());
            auto end = begin + packets_->getHeadersLength();

            std::copy(begin, end, ptr_);
            *reinterpret_cast<uint16_t*>(static_cast<cs::Byte*>(packetsEnd_->data()) + static_cast<uint32_t>(Offsets::FragmentId)) = packetsCount_;

            ptr_ += packets_->getHeadersLength();

            if (tail != nullptr) {
                insertBytes(tail, insertedSize);
            }
        }

        ++packetsCount_;
        ++packetsEnd_;
    }

    void insertBytes(char const* bytes, uint32_t size) {
        while (size > 0) {
            if (ptr_ == end_) {
                newPack();
            }

            const auto toPut = std::min(static_cast<uint32_t>(end_ - ptr_), size);
            std::copy(bytes, bytes + toPut, ptr_);

            size -= toPut;
            ptr_ += toPut;
            bytes += toPut;
        }
    }

    void insertBytes(const cs::Byte* bytes, uint32_t size) {
        insertBytes(reinterpret_cast<const char*>(bytes), size);
    }

    cs::Byte* ptr_ = nullptr;
    cs::Byte* end_ = nullptr;

    RegionAllocator* allocator_;

    Packet* packets_;
    uint16_t packetsCount_ = 0;
    Packet* packetsEnd_;
    bool finished_ = false;

    uint64_t id_ = 0;
    cs::PublicKey senderKey_;
};
}  // namespace cs

template <>
inline cs::IPackStream& cs::IPackStream::operator>>(std::string& str) {
    std::size_t size = 0;
    (*this) >> size;

    if (!good_) {
        return *this;
    }

    if (!isBytesAvailable(size)) {
        good_ = false;
    }
    else {
        auto nextPtr = ptr_ + size;
        str = std::string(ptr_, nextPtr);

        ptr_ = nextPtr;
    }

    return *this;
}

template <>
inline cs::IPackStream& cs::IPackStream::operator>>(cs::Bytes& bytes) {
    size_t size = 0;
    (*this) >> size;

    if (!good_) {
        return *this;
    }

    if (!isBytesAvailable(size)) {
        good_ = false;
    }
    else {
        auto nextPtr = ptr_ + size;
        bytes = cs::Bytes(ptr_, nextPtr);

        ptr_ = nextPtr;
    }

    return *this;
}

template <>
inline cs::IPackStream& cs::IPackStream::operator>>(csdb::Pool& pool) {
    cs::Bytes bytes;
    (*this) >> bytes;
    pool = csdb::Pool::from_binary(std::move(bytes));
    return *this;
}

template <>
inline cs::IPackStream& cs::IPackStream::operator>>(ip::address& addr) {
    if (!canPeek<uint8_t>()) {
        good_ = false;
    }
    else {
        if (*(ptr_++) & 1) {
            if (static_cast<uint32_t>(end_ - ptr_) < 16) {
                good_ = false;
            }
            else {
                ip::address_v6::bytes_type bt;

                for (auto& b : bt) {
                    (*this) >> b;
                }

                addr = ip::make_address_v6(bt);
            }
        }
        else {
            uint32_t ipnum;

            for (auto ptr = reinterpret_cast<cs::Byte*>(&ipnum) + 3; ptr >= reinterpret_cast<cs::Byte*>(&ipnum); --ptr) {
                (*this) >> *ptr;
            }

            addr = ip::make_address_v4(ipnum);
        }
    }

    return *this;
}

template <>
inline cs::IPackStream& cs::IPackStream::operator>>(cs::TransactionsPacketHash& hash) {
    cs::Bytes bytes;
    (*this) >> bytes;

    if (!good()) {
        return *this;
    }

    hash = cs::TransactionsPacketHash::fromBinary(std::move(bytes));
    return *this;
}

template <>
inline cs::IPackStream& cs::IPackStream::operator>>(cs::TransactionsPacket& packet) {
    cs::Bytes bytes;
    (*this) >> bytes;

    packet = cs::TransactionsPacket::fromBinary(bytes);
    return *this;
}

template <>
inline cs::IPackStream& cs::IPackStream::operator>>(csdb::PoolHash& hash) {
    cs::Bytes bytes;
    (*this) >> bytes;
    hash = csdb::PoolHash::from_binary(std::move(bytes));
    return *this;
}

template <>
inline cs::IPackStream& cs::IPackStream::operator>>(cs::BytesView& view) {
    size_t size = 0;
    (*this) >> size;

    if (!good_) {
        return *this;
    }

    if (!isBytesAvailable(size)) {
        good_ = false;
    }
    else {
        view = cs::BytesView(ptr_, size);
        ptr_ += size;
    }

    return *this;
}

template <>
inline cs::IPackStream& cs::IPackStream::operator>>(RegionPtr& regionPtr) {
    std::size_t size = regionPtr->size();

    if (!isBytesAvailable(size)) {
        good_ = false;
    }
    else {
        std::copy(ptr_, ptr_ + size, reinterpret_cast<char*>(regionPtr->data()));
        ptr_ += size;
    }

    return *this;
}

template <>
inline cs::OPackStream& cs::OPackStream::operator<<(const ip::address& ip) {
    (*this) << static_cast<cs::Byte>(ip.is_v6());

    if (ip.is_v6()) {
        auto bts = ip.to_v6().to_bytes();

        for (auto& b : bts) {
            (*this) << b;
        }
    }
    else {
        uint32_t ipnum = ip.to_v4().to_uint();

        for (auto ptr = reinterpret_cast<cs::Byte*>(&ipnum) + 3; ptr >= reinterpret_cast<cs::Byte*>(&ipnum); --ptr) {
            (*this) << *ptr;
        }
    }

    return *this;
}

template <>
inline cs::OPackStream& cs::OPackStream::operator<<(const std::string& str) {
    (*this) << str.size();
    insertBytes(str.data(), static_cast<uint32_t>(str.size()));
    return *this;
}

template <>
inline cs::OPackStream& cs::OPackStream::operator<<(const cs::Bytes& bytes) {
    (*this) << bytes.size();
    insertBytes(reinterpret_cast<const char*>(bytes.data()), static_cast<uint32_t>(bytes.size()));
    return *this;
}

template <>
inline cs::OPackStream& cs::OPackStream::operator<<(const csdb::Transaction& trans) {
    (*this) << trans.to_byte_stream();
    return *this;
}

template <>
inline cs::OPackStream& cs::OPackStream::operator<<(const csdb::Pool& pool) {
    uint32_t bSize;
    auto dataPtr = const_cast<csdb::Pool&>(pool).to_byte_stream(bSize);

    (*this) << static_cast<std::size_t>(bSize);
    insertBytes(static_cast<char*>(dataPtr), bSize);
    return *this;
}

template <>
inline cs::OPackStream& cs::OPackStream::operator<<(const cs::TransactionsPacketHash& hash) {
    (*this) << hash.toBinary();
    return *this;
}

template <>
inline cs::OPackStream& cs::OPackStream::operator<<(const cs::TransactionsPacket& packet) {
    (*this) << packet.toBinary();
    return *this;
}

template <>
inline cs::OPackStream& cs::OPackStream::operator<<(const csdb::PoolHash& hash) {
    (*this) << hash.to_binary();
    return *this;
}

template <>
inline cs::OPackStream& cs::OPackStream::operator<<(const RegionPtr& regionPtr) {
    insertBytes(reinterpret_cast<const char*>(regionPtr->data()), static_cast<uint32_t>(regionPtr->size()));
    return *this;
}
#endif  // PACKSTREAM_HPP
