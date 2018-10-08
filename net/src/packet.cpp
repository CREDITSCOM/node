/* Send blaming letters to @yrtimd */
#include <lz4.h>

#include "packet.hpp"

RegionAllocator Message::allocator_(1 << 26, 4);

enum Lengths {
  FragmentedHeader = 36
};

const cs::Hash& Packet::getHeaderHash() const {
  if (!headerHashed_) {
    headerHash_ = getBlake2Hash(static_cast<const char*>(data_.get()) + static_cast<uint32_t>(Offsets::FragmentsNum), Lengths::FragmentedHeader);
    headerHashed_ = true;
  }
  return headerHash_;
}

bool Packet::isHeaderValid() const {
  if (isFragmented()) {
    if (isNetwork()) return false;

    auto& frNum = getFragmentsNum();
    //LOG_WARN("FR: " << frNum << " vs " << getFragmentId() << " and " << PacketCollector::MaxFragments << ", then " << size() << " vs " << getHeadersLength());
    if (/*frNum > Packet::MaxFragments ||*/
        getFragmentId() >= frNum)
      return false;
  }

  return size() > getHeadersLength();
}

uint32_t Packet::getHeadersLength() const {
  if (!headersLength_) {
    headersLength_ = 1;  // Flags

    if (isFragmented())
      headersLength_+= 4;  // Min fragments & all fragments

    if (!isNetwork()) {
      headersLength_+= 40;  // Sender key + ID
      if (!isBroadcast() && !isDirect())
        headersLength_+= 32; // Receiver key
    }
  }

  return headersLength_;
}

MessagePtr PacketCollector::getMessage(const Packet& pack,
                                       bool& newFragmentedMsg) {
  if (!pack.isFragmented()) return MessagePtr();

  newFragmentedMsg = false;

  MessagePtr* msgPtr;
  MessagePtr msg;

  {
    SpinLock l(mLock_);
    msgPtr = &map_.tryStore(pack.getHeaderHash());
  }

  if (!*msgPtr) { // First time
    *msgPtr = msg = msgAllocator_.emplace();
    msg->packetsLeft_ = pack.getFragmentsNum();
    msg->packetsTotal_ = pack.getFragmentsNum();
    msg->headerHash_ = pack.getHeaderHash();
    newFragmentedMsg = true;
  }
  else msg = *msgPtr;

  {
    SpinLock l(msg->pLock_);
    auto goodPlace = msg->packets_ + pack.getFragmentId();
    if (!*goodPlace) {
      msg->maxFragment_ = std::max(pack.getFragmentsNum(), msg->maxFragment_);
      --msg->packetsLeft_;
      *goodPlace = pack;
    }

    //if (msg->packetsLeft_ % 100 == 0)
    //if (msg->packetsLeft_ == 0)
    //  LOG_WARN(msg->packetsLeft_ << " / " << msg->packetsTotal_);
  }

  return msg;
}

/* WARN: All the cases except FRAG + COMPRESSED have bugs in them */
void Message::composeFullData() const {
  if (getFirstPack().isFragmented()) {
    const Packet* pack = packets_;
    uint32_t headersLength = pack->getHeadersLength();

    uint32_t totalSize = headersLength;

    for (uint32_t i = 0; i < packetsTotal_; ++i, ++pack)
      totalSize+= (pack->size() - headersLength);

    fullData_ = allocator_.allocateNext(totalSize);

    uint8_t* data = static_cast<uint8_t*>(fullData_.get());
    pack = packets_;
    for (uint32_t i = 0; i < packetsTotal_; ++i, ++pack) {
      uint32_t cSize = pack->size() - (i == 0 ? 0 : headersLength);
      memcpy(data, ((const char*)pack->data()) + (i == 0 ? 0 : headersLength), cSize);
      data+= cSize;
    }
  }
}

Message::~Message() {
  /*auto pEnd = packets_ + packetsTotal_;
  for (auto ptr = packets_; ptr != pEnd; ++ptr)
    if (ptr) ptr->~Packet();

    memset(packets_, 0, sizeof(Packet*) * packetsTotal_);*/
}
