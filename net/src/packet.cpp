/* Send blaming letters to @yrtimd */
#include <snappy.h>

#include "packet.hpp"

RegionAllocator Message::allocator_(1 << 25, 2);

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
    //LOG_WARN("FR: " << frNum << " vs " << getFragmentId() << " and " << PacketCollector::MaxFragments << ", then " << size() << " vs " << getHeadersLength());
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
      headersLength_+= 40;  // Sender key + ID
      if (!isBroadcast())
        headersLength_+= 32; // Receiver key
    }
  }

  return headersLength_;
}

Message& PacketCollector::getMessage(const Packet& pack) {
  Message& msg = map_.tryStore(pack.getHeaderHash());
  if (!msg.packets_) { // First time
    msg.packets_ = activePtr_;
    msg.packetsLeft_ = pack.getFragmentsNum();
    msg.packetsTotal_ = pack.getFragmentsNum();

    memset(msg.packets_, 0, msg.packetsLeft_ * sizeof(Packet));

    activePtr_+= MaxFragments;
    if (activePtr_ == ptrsEnd_) activePtr_ = ptrs_;
  }

  auto goodPlace = msg.packets_ + pack.getFragmentId();
  if (!*goodPlace) {
    --msg.packetsLeft_;
    *goodPlace = pack;
  }

  return msg;
}

void Message::composeFullData() const {
  if (getFirstPack().isFragmented()) {
    uint32_t totalSize = 0;

    Packet* pack = packets_;
    uint32_t headersLength = pack->getHeadersLength();

    for (uint32_t i = 0; i < packetsTotal_; ++i, ++pack)
      totalSize+= pack->size() - headersLength;

    const uint32_t MsgHeaderSize = 1 + sizeof(RoundNum);

    fullData_ = allocator_.allocateNext(totalSize - MsgHeaderSize);

    uint8_t* data = static_cast<uint8_t*>(fullData_.get());
    pack = packets_;
    for (uint32_t i = 0; i < packetsTotal_; ++i, ++pack) {
      //LOG_WARN("-- " << byteStreamToHex((const char*)pack->data(), 100));
      uint32_t cSize = pack->size() - headersLength - (i == 0 ? MsgHeaderSize : 0);
      memcpy(data, pack->getMsgData() + (i == 0 ? MsgHeaderSize : 0), cSize);
      data+= cSize;
    }

    if (getFirstPack().isCompressed()) {
      size_t uncompressedSize;

      //LOG_WARN("fullData size: " << fullData_.size());

      snappy::GetUncompressedLength((const char*)fullData_.get(),
                                    fullData_.size(),
                                    &uncompressedSize);

      RegionPtr uncompressedData = allocator_.allocateNext(uncompressedSize + MsgHeaderSize);

      //LOG_WARN("Uncomressed block will be " << uncompressedSize);

      snappy::RawUncompress((const char*)fullData_.get(),
                            (size_t)fullData_.size(),
                            (char*)uncompressedData.get() + MsgHeaderSize);

      //LOG_WARN("Unpresult " << (t1 ? 1 : 0) << ", " << (t2 ? 1 : 0));

      memcpy(uncompressedData.get(), packets_->getMsgData(), MsgHeaderSize);
      fullData_ = uncompressedData;
    }
  }
  else {
    size_t uncompressedSize;
    snappy::GetUncompressedLength((const char*)(packets_->getMsgData()),
                                  (size_t)(packets_->getMsgSize()),
                                  &uncompressedSize);

    fullData_ = allocator_.allocateNext(uncompressedSize);

    snappy::RawUncompress((const char*)fullData_.get(),
                          (size_t)fullData_.size(),
                          (char*)fullData_.get());
  }
}

Message::~Message() {
  auto pEnd = packets_ + packetsTotal_;
  for (auto ptr = packets_; ptr != pEnd; ++ptr) {
    if (ptr) ptr->~Packet();
  }
}
