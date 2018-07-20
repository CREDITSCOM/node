/* Send blaming letters to @yrtimd */
#ifndef __PACKET_HPP__
#define __PACKET_HPP__
#include <boost/asio.hpp>

#include <lib/system/allocators.hpp>
#include <lib/system/hash.hpp>
#include <lib/system/keys.hpp>

using namespace boost::asio;

enum BaseFlags: uint8_t {
  NetworkMsg = 1,
  Fragmented = 1 << 1,
  Broadcast  = 1 << 2,
  Compressed = 1 << 3,
  Encrypted  = 1 << 4,
  Signed     = 1 << 5
};

enum class Offsets: uint32_t {
  FragmentId = 1,
  FragmentsNum = 3,
  SenderWhenFragmented = 7,
  SenderWhenSingle = 3,
  AddresseeWhenFragmented = 39,
  AddresseeWhenSingle = 35
};

class Packet {
public:
  const static std::size_t MaxSize = 1 << 16;

  Packet() { }
  Packet(RegionPtr&& data): data_(std::move(data)) { }

  bool isNetwork() const { return checkFlag(BaseFlags::NetworkMsg); }
  bool isFragmented() const { return checkFlag(BaseFlags::Fragmented); }
  bool isBroadcast() const { return checkFlag(BaseFlags::Broadcast); }

  bool isCompressed() const { return checkFlag(BaseFlags::Compressed); }

  const Hash& getHash() const {
    if (!hashed_) {
      hash_ = getBlake2Hash(data_.get(), data_.size());
      hashed_ = true;
    }
    return hash_;
  }

  bool addressedToMe(const PublicKey& myKey) const {
    return
      isNetwork() ||
      (isBroadcast() && !(getSender() == myKey)) ||
      getAddressee() == myKey;
  }

  const PublicKey& getSender() const { return getWithOffset<PublicKey>(isFragmented() ? Offsets::SenderWhenFragmented : Offsets::SenderWhenSingle); }
  const PublicKey& getAddressee() const { return getWithOffset<PublicKey>(isFragmented() ? Offsets::AddresseeWhenFragmented : Offsets::AddresseeWhenSingle); }

  const Hash& getHeaderHash() const;
  bool isHeaderValid() const;

  const uint16_t& getFragmentId() const { return getWithOffset<uint16_t>(Offsets::FragmentId); };
  const uint16_t& getFragmentsNum() const { return getWithOffset<uint16_t>(Offsets::FragmentsNum); };

  void* data() { return data_.get(); }
  const void* data() const { return data_.get(); }

  size_t size() const { return data_.size(); }

  const uint8_t* getMsgData() const { return static_cast<const uint8_t*>(data_.get()) + getHeadersLength(); }
  size_t getMsgSize() const { return size() - getHeadersLength(); }

  uint32_t getHeadersLength() const;

private:
  bool checkFlag(const BaseFlags flag) const {
    return *static_cast<const uint8_t*>(data_.get()) & flag;
  }

  template <typename T>
  const T& getWithOffset(const Offsets offset) const {
    return *(reinterpret_cast<const T*>(static_cast<const uint8_t*>(data_.get()) + static_cast<uint32_t>(offset)));
  }

  RegionPtr data_;

  mutable bool hashed_ = false;
  mutable Hash hash_;

  mutable bool headerHashed_ = false;
  mutable Hash headerHash_;

  mutable uint32_t headersLength_ = 0;

  friend class IPacMan;
};

typedef Packet* PacketPtr;

class Message {
public:
  bool isComplete() const { return packetsLeft_ == 0; }

  const Packet& getFirstPack() const { return **packets_; }

  const uint8_t* getFullData() const {
    if (!fullData_) composeFullData();
    return static_cast<const uint8_t*>(fullData_.get());
  }

  size_t getFullSize() const {
    if (!fullData_) composeFullData();
    return fullData_.size();
  }

private:
  static RegionAllocator allocator_;

  void composeFullData() const;

  uint32_t packetsLeft_;
  uint32_t packetsTotal_;
  PacketPtr* packets_ = nullptr;

  mutable RegionPtr fullData_;

  friend class PacketCollector;
};

class PacketCollector {
public:
  static const uint32_t MaxParallelCollections = 1024;
  static const uint32_t MaxFragments = 1024;

  PacketCollector():
    ptrs_(static_cast<PacketPtr**>(malloc(MaxParallelCollections * MaxFragments * sizeof(PacketPtr*)))),
    activePtr_(ptrs_),
    ptrsEnd_(ptrs_ + MaxParallelCollections * MaxFragments) {
  }

  Message& getMessage(PacketPtr pack);

private:
  FixedHashMap<Hash, Message, uint16_t, MaxParallelCollections> map_;

  Message lastMessage_;

  PacketPtr** ptrs_;
  PacketPtr** activePtr_;
  PacketPtr** ptrsEnd_;
};

#endif // __PACKET_HPP__
