/* Send blaming letters to @yrtimd */
#include <snappy/snappy.h>

#include "packet.hpp"

RegionAllocator Message::allocator_(1 << 25, 1);

enum Lengths {
  FragmentedHeader = 36
};

const Hash& Packet::getHeaderHash() const {
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
    if (frNum > PacketCollector::MaxFragments ||
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
      headersLength_+= 34;  // Sender key + ID
      if (!isBroadcast())
        headersLength_+= 32; // Receiver key
    }
  }

  return headersLength_;
}

Message& PacketCollector::getMessage(PacketPtr pack) {
  if (pack->isFragmented()) {
    Message& msg = map_.tryStore(pack->getHeaderHash());
    if (!msg.packets_) { // First time
      msg.packets_ = *activePtr_;
      msg.packetsLeft_ = pack->getFragmentsNum();
      msg.packetsTotal_ = pack->getFragmentsNum();

      memset(msg.packets_, 0, msg.packetsLeft_ * sizeof(PacketPtr*));

      activePtr_+= MaxFragments;
      if (activePtr_ == ptrsEnd_) activePtr_ = ptrs_;
    }

    auto goodPlace = msg.packets_ + pack->getFragmentId();
    if (!goodPlace) {
      --msg.packetsLeft_;
      *goodPlace = pack;
    }

    return msg;
  }

  lastMessage_ = Message();
  lastMessage_.packetsLeft_ = 0;
  lastMessage_.packetsTotal_ = 1;
  lastMessage_.packets_[0] = pack;

  return lastMessage_;
}

void Message::composeFullData() const {
  if (getFirstPack().isFragmented()) {
    uint32_t totalSize = 0;

    PacketPtr* pack = packets_;
    uint32_t headersLength = (*pack)->getHeadersLength();

    for (uint32_t i = 0; i < packetsTotal_; ++i, ++pack)
      totalSize+= (*pack)->size() - headersLength;

    fullData_ = allocator_.allocateNext(totalSize);

    uint8_t* data = static_cast<uint8_t*>(fullData_.get());
    pack = packets_;

    for (uint32_t i = 0; i < packetsTotal_; ++i, ++pack) {
      uint32_t cSize = (*pack)->size() - headersLength;
      memcpy(data, (*pack)->getMsgData(), cSize);
      data+= cSize;
    }

    if (getFirstPack().isCompressed()) {
      size_t uncompressedSize;

      snappy::GetUncompressedLength((const char*)fullData_.get(),
                                    (size_t)fullData_.size(),
                                    &uncompressedSize);
      RegionPtr uncompressedData = allocator_.allocateNext(uncompressedSize);

      snappy::RawUncompress((const char*)fullData_.get(),
                            (size_t)fullData_.size(),
                            (char*)uncompressedData.get());

      fullData_ = uncompressedData;
    }
  }
  else {
    size_t uncompressedSize;
    snappy::GetUncompressedLength((const char*)(*packets_)->getMsgData(),
                                  (size_t)(*packets_)->getMsgSize(),
                                  &uncompressedSize);

    fullData_ = allocator_.allocateNext(uncompressedSize);

    snappy::RawUncompress((const char*)fullData_.get(),
                          (size_t)fullData_.size(),
                          (char*)fullData_.get());
  }
}
