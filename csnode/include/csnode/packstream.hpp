#ifndef PACKSTREAM_HPP
#define PACKSTREAM_HPP

#include <algorithm>
#include <type_traits>
#include <cstring>

#include <csnode/nodecore.hpp>
#include <csnode/transactionspacket.hpp>

#include <csdb/pool.h>
#include <csdb/transaction.h>

#include <lib/system/hash.hpp>
#include <lib/system/keys.hpp>

#include <net/packet.hpp>
#include "solver/solver.hpp"

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
    return *(T*)ptr_;
  }

  template <typename T>
  void skip() {
    ptr_ += sizeof(T);
  }

  template <typename T>
  void safeSkip(uint32_t num = 1) {
    auto size = sizeof(T) * num;

    if ((uint32_t)(end_ - ptr_) < size) {
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
    if (static_cast<uint32_t>(end_ - ptr_) < Length) {
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
    if (static_cast<uint32_t>(end_ - ptr_) < Length) {
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
    std::size_t size;
    (*this) >> size;

    std::vector<T, A> entity;

    for (std::size_t i = 0; i < size; ++i) {
      T element;
      (*this) >> element;

      entity.push_back(element);
    }

    if (entity.size() != size) {
      cserror() << "Pack stream -> vector parsing failed";
      return *this;
    }

    vector = std::move(entity);

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
    return end_ - ptr_;
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
  , packets_(new Packet[Packet::MaxFragments]())
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
    *static_cast<cs::Byte*>(ptr_) = flags;
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
      *((T*)ptr_) = value;
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

  template <typename T, typename A>
  cs::OPackStream& operator<<(const std::vector<T, A>& vector) {
    (*this) << vector.size();

    for (const auto& element : vector) {
      (*this) << element;
    }

    return *this;
  }

  Packet* getPackets() {
    if (!finished_) {
      allocator_->shrinkLast(static_cast<uint32_t>(ptr_ - static_cast<cs::Byte*>((packetsEnd_ - 1)->data())));

      if (packetsCount_ > 1) {
        for (auto p = packets_; p != packetsEnd_; ++p) {
          cs::Byte* data = static_cast<cs::Byte*>(p->data());

          if (!p->isFragmented()) {
            csdebug() << "No Fragmented flag for packets";
            *data |= BaseFlags::Fragmented;
          }

          *reinterpret_cast<uint16_t*>(data + Offsets::FragmentId + sizeof(packetsCount_)) = packetsCount_;
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
    new (packetsEnd_) Packet(allocator_->allocateNext(Packet::MaxSize));

    ptr_ = static_cast<cs::Byte*>(packetsEnd_->data());
    end_ = ptr_ + packetsEnd_->size();

    if (packetsEnd_ != packets_) {
      std::copy(static_cast<cs::Byte*>(packets_->data()),
                static_cast<cs::Byte*>(packets_->data()) + packets_->getHeadersLength(), ptr_);
      *reinterpret_cast<uint16_t*>(static_cast<cs::Byte*>(packetsEnd_->data()) +
                                   static_cast<uint32_t>(Offsets::FragmentId)) = packetsCount_;

      ptr_ += packets_->getHeadersLength();
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

  auto nextPtr = ptr_ + size;
  str = std::string(ptr_, nextPtr);

  ptr_ = nextPtr;
  return *this;
}

template <>
inline cs::IPackStream& cs::IPackStream::operator>>(cs::Bytes& bytes) {
  std::size_t size;
  (*this) >> size;

  auto nextPtr = ptr_ + size;
  bytes = std::vector<uint8_t>(ptr_, nextPtr);

  ptr_ = nextPtr;
  return *this;
}

template <>
inline cs::IPackStream& cs::IPackStream::operator>>(csdb::Pool& pool) {
  cs::Bytes bytes;
  (*this) >> bytes;
  pool = csdb::Pool::from_binary(bytes);
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

  hash = cs::TransactionsPacketHash::fromBinary(bytes);
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
inline cs::IPackStream& cs::IPackStream::operator>>(cs::HashVector& hashVector) {
  (*this) >> hashVector.sender >> hashVector.hash >> hashVector.signature;
  return *this;
}

template <>
inline cs::IPackStream& cs::IPackStream::operator>>(cs::HashMatrix& hashMatrix) {
  (*this) >> hashMatrix.sender;

  for (std::size_t i = 0; i < hashVectorCount; ++i) {
    (*this) >> hashMatrix.hashVector[i];
  }

  (*this) >> hashMatrix.signature;

  return *this;
}

template <>
inline cs::IPackStream& cs::IPackStream::operator>>(csdb::PoolHash& hash) {
  cs::Bytes bytes;
  (*this) >> bytes;
  hash = csdb::PoolHash::from_binary(bytes);
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
inline cs::OPackStream& cs::OPackStream::operator<<(const cs::HashVector& hashVector) {
  (*this) << hashVector.sender << hashVector.hash << hashVector.signature;
  return *this;
}

template <>
inline cs::OPackStream& cs::OPackStream::operator<<(const cs::HashMatrix& hashMatrix) {
  (*this) << hashMatrix.sender;

  for (std::size_t i = 0; i < hashVectorCount; ++i) {
    (*this) << hashMatrix.hashVector[i];
  }

  (*this) << hashMatrix.signature;
  return *this;
}

template <>
inline cs::OPackStream& cs::OPackStream::operator<<(const csdb::PoolHash& hash) {
  (*this) << hash.to_binary();
  return *this;
}

#endif  // PACKSTREAM_HPP
