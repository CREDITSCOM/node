/* Send blaming letters to @yrtimd */
#ifndef __PACKET_HPP__
#define __PACKET_HPP__
#include <boost/asio.hpp>

#include <lib/system/allocators.hpp>
#include <lib/system/hash.hpp>
#include <lib/system/keys.hpp>
#include <lib/system/common.hpp>

#include <iostream>

using namespace boost::asio;

enum BaseFlags: uint8_t {
  NetworkMsg = 1,
  Fragmented = 1 << 1,
  Broadcast  = 1 << 2,
  Compressed = 1 << 3,
  Encrypted  = 1 << 4,
  Signed     = 1 << 5,
  Direct     = 1 << 6
};

enum Offsets: uint32_t {
  FragmentId = 1,
  FragmentsNum = 3,
  IdWhenFragmented = 5,
  IdWhenSingle = 1,
  SenderWhenFragmented = 13,
  SenderWhenSingle = 9,
  AddresseeWhenFragmented = 45,
  AddresseeWhenSingle = 41
};

enum MsgTypes: uint8_t {
  RoundTableSS,
  Transactions,
  FirstTransaction,
  TransactionList,
  ConsVector,
  ConsMatrix,
  NewBlock,
  BlockHash,
  BlockRequest,
  RequestedBlock,
  TLConfirmation,
  ConsVectorRequest,
  ConsMatrixRequest,
  ConsTLRequest,
  RoundTableRequest,
  NewBadBlock,
  BigBang = 35,
  TransactionPacket,
  TransactionsPacketRequest,
  TransactionsPacketReply,
  NewCharacteristic,
  RoundTable,
  WriterNotification
};

using RoundNum = uint32_t;

class Packet {
public:
  static const uint32_t MaxSize = 1 << 10;
  static const uint32_t MaxFragments = 1 << 12;

  static const uint32_t SmartRedirectTreshold = 100000;

  Packet() = default;
  explicit Packet(RegionPtr&& data): data_(std::move(data)) { }

  bool isNetwork() const { return checkFlag(BaseFlags::NetworkMsg); }
  bool isFragmented() const { return checkFlag(BaseFlags::Fragmented); }
  bool isBroadcast() const { return checkFlag(BaseFlags::Broadcast); }

  bool isCompressed() const { return checkFlag(BaseFlags::Compressed); }
  bool isDirect() const { return checkFlag(BaseFlags::Direct); }

  const cs::Hash& getHash() const {
    if (!hashed_) {
      hash_ = getBlake2Hash(data_.get(), data_.size());
      hashed_ = true;
    }
    return hash_;
  }

  bool addressedToMe(const cs::PublicKey& myKey) const {
    return
      isNetwork() || isDirect() ||
      (isBroadcast() && !(getSender() == myKey)) ||
      getAddressee() == myKey;
  }

  const cs::PublicKey& getSender() const { return getWithOffset<cs::PublicKey>(isFragmented() ? Offsets::SenderWhenFragmented : Offsets::SenderWhenSingle); }
  const cs::PublicKey& getAddressee() const { return getWithOffset<cs::PublicKey>(isFragmented() ? Offsets::AddresseeWhenFragmented : Offsets::AddresseeWhenSingle); }

  const uint64_t& getId() const { return getWithOffset<uint64_t>(isFragmented() ? Offsets::IdWhenFragmented : Offsets::IdWhenSingle); }

  const cs::Hash& getHeaderHash() const;
  bool isHeaderValid() const;

  const uint16_t& getFragmentId() const { return getWithOffset<uint16_t>(Offsets::FragmentId); }
  const uint16_t& getFragmentsNum() const { return getWithOffset<uint16_t>(Offsets::FragmentsNum); }

  MsgTypes getType() const { return getWithOffset<MsgTypes>(getHeadersLength()); }
  RoundNum getRoundNum() const { return getWithOffset<RoundNum>(getHeadersLength() + 1); }

  void* data() { return data_.get(); }
  const void* data() const { return data_.get(); }

  size_t size() const { return data_.size(); }

  const uint8_t* getMsgData() const { return static_cast<const uint8_t*>(data_.get()) + getHeadersLength(); }
  size_t getMsgSize() const { return size() - getHeadersLength(); }

  uint32_t getHeadersLength() const;

  explicit operator bool() { return data_; }

private:
  bool checkFlag(const BaseFlags flag) const {
    return (*static_cast<const uint8_t*>(data_.get()) & flag) != 0;
  }

  template <typename T>
  const T& getWithOffset(const uint32_t offset) const {
    return *(reinterpret_cast<const T*>(static_cast<const uint8_t*>(data_.get()) + offset));
  }

  RegionPtr data_;

  mutable bool hashed_ = false;
  mutable cs::Hash hash_;

  mutable bool headerHashed_ = false;
  mutable cs::Hash headerHash_;

  mutable uint32_t headersLength_ = 0;

  friend class IPacMan;
  friend class Message;
};

typedef Packet* PacketPtr;

class Message {
public:
  Message() = default;

  Message(Message&&) = default;
  Message& operator=(Message&&) = default;

  Message(const Message&) = delete;
  Message& operator=(const Message&) = delete;

  ~Message();

  bool isComplete() const { return packetsLeft_ == 0; }

  const Packet& getFirstPack() const { return *packets_; }

  const uint8_t* getFullData() const {
    if (!fullData_) {
      composeFullData();
    }
    return static_cast<const uint8_t*>(fullData_.get()) + packets_->getHeadersLength();
  }

  size_t getFullSize() const {
    if (!fullData_) {
        composeFullData();
    }
    return fullData_.size() - packets_->getHeadersLength();
  }

  Packet extractData() const {
    if (!fullData_) composeFullData();
    Packet result(std::move(fullData_));
    result.headersLength_ = packets_->getHeadersLength();
    return result;
  }

private:
  static RegionAllocator allocator_;

  void composeFullData() const;

  std::atomic_flag pLock_ = ATOMIC_FLAG_INIT;

  uint32_t packetsLeft_;
  uint32_t packetsTotal_ = 0;

  uint16_t maxFragment_ = 0;
  Packet packets_[Packet::MaxFragments];

  cs::Hash headerHash_;

  mutable RegionPtr fullData_;

  friend class PacketCollector;
  friend class Transport;
  friend class Network;
};

using MessagePtr = MemPtr<TypedSlot<Message>>;

class PacketCollector {
public:
  static const uint32_t MaxParallelCollections = 1024;

  PacketCollector():
    msgAllocator_(MaxParallelCollections + 1) { }

  MessagePtr getMessage(const Packet&, bool&);

private:
  TypedAllocator<Message> msgAllocator_;

  std::atomic_flag mLock_ = ATOMIC_FLAG_INIT;
  FixedHashMap<cs::Hash, MessagePtr, uint16_t, MaxParallelCollections> map_;

  Message lastMessage_;
  friend class Network;
};

std::ostream& operator<< (std::ostream& os, const Packet& packet);

#endif // __PACKET_HPP__
