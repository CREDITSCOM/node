/* Send blaming letters to @yrtimd */
#ifndef __PACKSTREAM_HPP__
#define __PACKSTREAM_HPP__
#include <algorithm>
#include <cstring>

#include <csdb/pool.h>
#include <csdb/transaction.h>

#include <lib/system/hash.hpp>
#include <lib/system/keys.hpp>
#include <net/packet.hpp>
#include "solver/solver.hpp"

class IPackStream {
 public:
  void init(const uint8_t* ptr, const size_t size) {
    ptr_  = ptr;
    end_  = ptr_ + size;
    good_ = true;
  }

  template <typename T>
  bool canPeek() const {
    return (uint32_t)(end_ - ptr_) >= sizeof(T);
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

    if ((uint32_t)(end_ - ptr_) < size)
      good_ = false;
    else
      ptr_ += size;
  }

  template <typename T>
  IPackStream& operator>>(T& cont) {
    if (!canPeek<T>())
      good_ = false;
    else {
      cont = peek<T>();
      skip<T>();
    }

    return *this;
  }

  template <size_t Length>
  IPackStream& operator>>(FixedString<Length>& str) {
    if ((uint32_t)(end_ - ptr_) < Length)
      good_ = false;
    else {
      std::memcpy(str.data(), ptr_, Length);
      ptr_ += Length;
    }

    return *this;
  }

  template <size_t Length>
  IPackStream& operator>>(cs::ByteArray<Length>& byteArray) {
      if ((uint32_t)(end_ - ptr_) < Length)
          good_ = false;
      else {
          std::memcpy(byteArray.data(), ptr_, Length);
          ptr_ += Length;
      }

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
  const uint8_t* getCurrPtr() const {
    return ptr_;
  }

 private:
  const uint8_t* ptr_;
  const uint8_t* end_;
  bool           good_ = false;
};

class OPackStream {
 public:
  OPackStream(RegionAllocator* allocator, const cs::PublicKey& myKey)
  : allocator_(allocator)
  , packets_(static_cast<Packet*>(calloc(Packet::MaxFragments, sizeof(Packet))))
  , packetsEnd_(packets_)
  , senderKey_(myKey) {
  }

  void init(uint8_t flags) {
    clear();
    ++id_;

    newPack();
    *static_cast<uint8_t*>(ptr_) = flags;
    ++ptr_;

    if (flags & BaseFlags::Fragmented)
      *this << (uint16_t)0 << packetsCount_;

    if (!(flags & BaseFlags::NetworkMsg))
      *this << id_ << senderKey_;
  }

  void init(uint8_t flags, const cs::PublicKey& receiver) {
    init(flags);
    *this << receiver;
  }

  void clear() {
    for (auto ptr = packets_; ptr != packetsEnd_; ++ptr)
      ptr->~Packet();

    packetsCount_ = 0;
    finished_     = false;
    packetsEnd_   = packets_;
  }

  template <typename T>
  OPackStream& operator<<(const T& d) {
    static_assert(sizeof(T) <= Packet::MaxSize, "Type too long");

    const uint32_t left = end_ - ptr_;
    if (left >= sizeof(T)) {
      *((T*)ptr_) = d;
      ptr_ += sizeof(T);
    } else {  // On border
      memcpy(ptr_, &d, left);
      newPack();
      memcpy(ptr_, ((uint8_t*)&d) + left, sizeof(T) - left);
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

  Packet* getPackets() {
    if (!finished_) {
      allocator_->shrinkLast(ptr_ - static_cast<uint8_t*>((packetsEnd_ - 1)->data()));

      if (packetsCount_ > 1)
        for (auto p = packets_; p != packetsEnd_; ++p)
          *reinterpret_cast<uint16_t*>(static_cast<uint8_t*>(p->data()) + static_cast<uint32_t>(Offsets::FragmentId) +
                                       sizeof(packetsCount_)) = packetsCount_;

      finished_ = true;
    }

    return packets_;
  }

  uint32_t getPacketsCount() {
    return packetsCount_;
  }

  uint8_t* getCurrPtr() {
    return ptr_;
  }
  uint32_t getCurrSize() const {
    return ptr_ - (uint8_t*)((packetsEnd_ - 1)->data());
  }


 private:
  void newPack() {
    new (packetsEnd_) Packet(allocator_->allocateNext(Packet::MaxSize));

    ptr_ = static_cast<uint8_t*>(packetsEnd_->data());
    end_ = ptr_ + packetsEnd_->size();

    if (packetsEnd_ != packets_) {  // Not the first one
      memcpy(ptr_, packets_->data(), packets_->getHeadersLength());

      *reinterpret_cast<uint16_t*>(static_cast<uint8_t*>(packetsEnd_->data()) +
                                   static_cast<uint32_t>(Offsets::FragmentId)) = packetsCount_;

      ptr_ += packets_->getHeadersLength();
    }

    ++packetsCount_;
    ++packetsEnd_;
  }

  void insertBytes(char const* bytes, uint32_t size) {
    while (size > 0) {
      if (ptr_ == end_)
        newPack();

      const auto toPut = std::min((uint32_t)(end_ - ptr_), size);
      memcpy(ptr_, bytes, toPut);
      size -= toPut;
      ptr_ += toPut;
      bytes += toPut;
    }
  }

  void insertBytes(const unsigned char* bytes, uint32_t size) {
    insertBytes(reinterpret_cast<const char*>(bytes), size);
  }

  uint8_t* ptr_;
  uint8_t* end_;

  RegionAllocator* allocator_;

  Packet*  packets_;
  uint16_t packetsCount_ = 0;
  Packet*  packetsEnd_;
  bool     finished_ = false;

  uint64_t  id_ = 0;
  cs::PublicKey senderKey_;
};

template <>
inline IPackStream& IPackStream::operator>>(std::string& str) {
  str  = std::string(ptr_, end_);
  ptr_ = end_;
  return *this;
}

template <>
inline IPackStream& IPackStream::operator>>(csdb::Transaction& cont) {
  cont = csdb::Transaction::from_byte_stream((char*)ptr_, (size_t)(end_ - ptr_));
  ptr_ = end_;
  return *this;
}

template <>
inline IPackStream& IPackStream::operator>>(csdb::Pool& pool) {
  uint32_t uncompressedSize;
  *this >> uncompressedSize;
  pool = csdb::Pool::from_lz4_byte_stream(reinterpret_cast<const char*>(ptr_), end_ - ptr_, uncompressedSize);
  ptr_ = end_;
  return *this;
}

template <>
inline IPackStream& IPackStream::operator>>(cs::Bytes& bytes) {
  bytes = std::vector<uint8_t>(ptr_, end_);
  ptr_ = end_;
  return *this;
}

template <>
inline IPackStream& IPackStream::operator>>(ip::address& addr) {
  if (!canPeek<uint8_t>()) {
    good_ = false;
  } else {
    if (*(ptr_++) & 1) {
      if ((uint32_t)(end_ - ptr_) < 16) {
        good_ = false;
      } else {
        ip::address_v6::bytes_type bt;

        for (auto& b : bt)
          *this >> b;

        addr = ip::make_address_v6(bt);
      }
    } else {
      uint32_t ipnum;
      for (auto ptr = reinterpret_cast<uint8_t*>(&ipnum) + 3; ptr >= reinterpret_cast<uint8_t*>(&ipnum); --ptr)
        *this >> *ptr;
      addr = ip::make_address_v4(ipnum);
    }
  }

  return *this;
}

template <>
inline OPackStream& OPackStream::operator<<(const ip::address& ip) {
  *this << (uint8_t)(ip.is_v6());
  if (ip.is_v6()) {
    auto bts = ip.to_v6().to_bytes();
    for (auto& b : bts)
      *this << b;
  } else {
    uint32_t ipnum = ip.to_v4().to_uint();
    for (auto ptr = reinterpret_cast<uint8_t*>(&ipnum) + 3; ptr >= reinterpret_cast<uint8_t*>(&ipnum); --ptr)
      *this << *ptr;
  }
  return *this;
}

template <>
inline OPackStream& OPackStream::operator<<(const std::string& str) {
  insertBytes(str.data(), str.size());
  return *this;
}

template <>
inline OPackStream& OPackStream::operator<<(const csdb::Transaction& trans) {
  auto byteArray = trans.to_byte_stream();
  insertBytes((char*)byteArray.data(), byteArray.size());
  return *this;
}

template <>
inline OPackStream& OPackStream::operator<<(const csdb::Pool& pool) {
  uint32_t bSize;
  char* dataPtr = const_cast<csdb::Pool&>(pool).to_byte_stream(bSize);
  insertBytes(dataPtr, bSize);
  return *this;
}

template <>
inline OPackStream& OPackStream::operator<<(const cs::Bytes& bytes) {
  insertBytes((const char*)bytes.data(), bytes.size());
  return *this;
}

#endif  // __PACKSTREAM_HPP__
