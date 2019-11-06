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

    static inline RegionAllocator allocator_ = RegionAllocator{};
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

/*template <>
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
}*/

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
inline cs::IPackStream& cs::IPackStream::operator>>(CompressedRegion& region) {
    std::size_t binarySize = 0;
    (*this) >> binarySize;

    CompressedRegion::SizeType size = 0;
    (*this) >> size;

    if (!isBytesAvailable(size)) {
        good_ = false;
    }
    else {
        RegionPtr regionPtr = allocator_.allocateNext(size);

        std::copy(ptr_, ptr_ + size, reinterpret_cast<char*>(regionPtr->data()));
        ptr_ += size;

        region = CompressedRegion { std::move(regionPtr), binarySize };
    }

    return *this;
}
#endif  // PACKSTREAM_HPP
