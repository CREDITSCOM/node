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

enum Offsets: uint32_t {
  FragmentId = 1,
  FragmentsNum = 3,
  SenderWhenFragmented = 13,
  SenderWhenSingle = 9,
  AddresseeWhenFragmented = 45,
  AddresseeWhenSingle = 41
};

enum MsgTypes: uint8_t {
  RoundTable,
  Transactions,
  FirstTransaction,
  TransactionList,
  ConsVector,
  ConsMatrix,
  NewBlock,
  BlockHash
};

class Packet {
public:
  const static std::size_t MaxSize = 1 << 15;

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

  MsgTypes getType() const { return getWithOffset<MsgTypes>(getHeadersLength()); }

  void* data() { return data_.get(); }
  const void* data() const { return data_.get(); }

  size_t size() const { return data_.size(); }

  const uint8_t* getMsgData() const { return static_cast<const uint8_t*>(data_.get()) + getHeadersLength(); }
  size_t getMsgSize() const { return size() - getHeadersLength(); }

  uint32_t getHeadersLength() const;

  operator bool() { return data_; }

private:
  bool checkFlag(const BaseFlags flag) const {
    return *static_cast<const uint8_t*>(data_.get()) & flag;
  }

  template <typename T>
  const T& getWithOffset(const uint32_t offset) const {
    return *(reinterpret_cast<const T*>(static_cast<const uint8_t*>(data_.get()) + offset));
  }

  RegionPtr data_;

  mutable bool hashed_ = false;
  mutable Hash hash_;

  mutable bool headerHashed_ = false;
  mutable Hash headerHash_;

  mutable uint32_t headersLength_ = 0;

  friend class IPacMan;
  friend class Message;
};

typedef Packet* PacketPtr;

class Message {
public:
  ~Message();

  bool isComplete() const { return packetsLeft_ == 0; }

  const Packet& getFirstPack() const { return *packets_; }

  const uint8_t* getFullData() const {
    if (!fullData_) composeFullData();
    return static_cast<const uint8_t*>(fullData_.get());
  }

  size_t getFullSize() const {
    if (!fullData_) composeFullData();
    return fullData_.size();
  }

  Packet extractData() {
    if (!fullData_) composeFullData();
    Packet result(std::move(fullData_));
    result.headersLength_ = packets_->headersLength_;
    return result;
  }

private:
  static RegionAllocator allocator_;

  void composeFullData() const;

  uint32_t packetsLeft_;
  uint32_t packetsTotal_;
  Packet* packets_ = nullptr;

  mutable RegionPtr fullData_;

  friend class PacketCollector;
};

class PacketCollector {
public:
  static const uint32_t MaxParallelCollections = 8;
  static const uint32_t MaxFragments = 1 << 14;

  PacketCollector():
    ptrs_(static_cast<Packet*>(malloc(MaxParallelCollections * MaxFragments * sizeof(Packet)))),
    activePtr_(ptrs_),
    ptrsEnd_(ptrs_ + MaxParallelCollections * MaxFragments) {
  }

  Message& getMessage(const Packet& pack);

private:
  FixedHashMap<Hash, Message, uint16_t, MaxParallelCollections> map_;

  Message lastMessage_;

  Packet* ptrs_;
  Packet* activePtr_;
  Packet* ptrsEnd_;
};

#endif // __PACKET_HPP__
