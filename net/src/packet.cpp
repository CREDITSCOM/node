/* Send blaming letters to @yrtimd */
#include <lz4.h>

#include <lib/system/utils.hpp>
#include "packet.hpp"
#include "transport.hpp"  // for NetworkCommand

RegionAllocator Message::allocator_(1 << 26, 4);

enum Lengths
{
  FragmentedHeader = 36
};

const cs::Hash& Packet::getHeaderHash() const {
  if (!headerHashed_) {
    headerHash_ = generateHash(static_cast<const char*>(data_.get()) + static_cast<uint32_t>(Offsets::FragmentsNum),
                                Lengths::FragmentedHeader);
    headerHashed_ = true;
  }
  return headerHash_;
}

bool Packet::isHeaderValid() const {
  if (isFragmented()) {
    if (isNetwork())
      return false;

    const auto& frNum = getFragmentsNum();
    const auto& frId = getFragmentId();
    //LOG_WARN("FR: " << frNum << " vs " << getFragmentId() << " and " << PacketCollector::MaxFragments << ", then " << size() << " vs " << getHeadersLength());
    if(/*frNum > Packet::MaxFragments ||*/frId >= frNum) {
        return false;
    }
  }

  return size() > getHeadersLength();
}

uint32_t Packet::getHeadersLength() const {
  if (!headersLength_) {
    headersLength_ = 1;  // Flags

    if (isFragmented())
      headersLength_ += 4;  // Min fragments & all fragments

    if (!isNetwork()) {
      headersLength_ += 40;  // Sender key + ID
      if (!isBroadcast() && !isNeighbors())
        headersLength_ += 32;  // Receiver key
    }
  }

  return headersLength_;
}

MessagePtr PacketCollector::getMessage(const Packet& pack, bool& newFragmentedMsg) {
  if (!pack.isFragmented())
    return MessagePtr();

  newFragmentedMsg = false;

  MessagePtr* msgPtr;
  MessagePtr msg;

  {
    cs::SpinGuard l(mLock_);
    msgPtr = &map_.tryStore(pack.getHeaderHash());
  }

  if (!*msgPtr) {  // First time
    *msgPtr = msg = msgAllocator_.emplace();
    msg->packetsLeft_ = pack.getFragmentsNum();
    msg->packetsTotal_ = pack.getFragmentsNum();
    msg->headerHash_ = pack.getHeaderHash();
    newFragmentedMsg = true;
  }
  else
    msg = *msgPtr;

  {
    cs::SpinGuard l(msg->pLock_);
    auto goodPlace = msg->packets_ + pack.getFragmentId();
    if (!*goodPlace) {
      msg->maxFragment_ = std::max(pack.getFragmentsNum(), msg->maxFragment_);
      --msg->packetsLeft_;
      *goodPlace = pack;
    }

    //if (msg->packetsLeft_ % 100 == 0)
    // log significantly-fragmented packets:
    if(msg->packetsTotal_ >= 20) {
        if(msg->packetsLeft_ != 0) {
            csdetails() << "COLLECT> ready " << msg->packetsTotal_ - msg->packetsLeft_ << " / " << msg->packetsTotal_;
        }
        else {
            csdetails() << "COLLECT> complete " << msg->packetsTotal_;
        }
    }
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
      totalSize += (pack->size() - headersLength);

    fullData_ = allocator_.allocateNext(totalSize);

    uint8_t* data = static_cast<uint8_t*>(fullData_.get());
    pack = packets_;
    for (uint32_t i = 0; i < packetsTotal_; ++i, ++pack) {
      uint32_t cSize = cs::numeric_cast<uint32_t>(pack->size()) - (i == 0 ? 0 : headersLength);
      memcpy(data, (reinterpret_cast<const char*>(pack->data())) + (i == 0 ? 0 : headersLength), cSize);
      data += cSize;
    }
  }
}

Message::~Message() {
  /*auto pEnd = packets_ + packetsTotal_;
  for (auto ptr = packets_; ptr != pEnd; ++ptr)
    if (ptr) ptr->~Packet();

    memset(packets_, 0, sizeof(Packet*) * packetsTotal_);*/
}

const char* getMsgTypesString(MsgTypes messageType) {
  switch (messageType) {
    default:
      return "-";
    case RoundTableSS:
      return "RoundTableSS";
    case Transactions:
      return "Transactions";
    case FirstTransaction:
      return "FirstTransaction";
    case TransactionList:
      return "TransactionList";
    case ConsVector:
      return "ConsVector";
    case ConsMatrix:
      return "ConsMatrix";
    case NewBlock:
      return "NewBlock";
    case BlockHash:
      return "BlockHash";
    case BlockRequest:
      return "BlockRequest";
    case RequestedBlock:
      return "RequestedBlock";
    case TLConfirmation:
      return "TLConfirmation";
    case ConsVectorRequest:
      return "ConsVectorRequest";
    case ConsMatrixRequest:
      return "ConsMatrixRequest";
    case ConsTLRequest:
      return "ConsTLRequest";
    case RoundTableRequest:
      return "RoundTableRequest";
    case NewBadBlock:
      return "NewBadBlock";
    case BigBang:
      return "BigBang";
    case TransactionPacket:
      return "TransactionPacket";
    case TransactionsPacketRequest:
      return "TransactionsPacketRequest";
    case TransactionsPacketReply:
      return "TransactionsPacketReply";
    case NewCharacteristic:
      return "NewCharacteristic";
    case RoundTable:
      return "RoundTable";
    case WriterNotification:
      return "WriterNotification";
    case NodeStopRequest:
      return "NodeStopRequest";
  }
}

const char* getNetworkCommandString(NetworkCommand command) {
  switch (command) {
    default:
      return "-";
    case NetworkCommand::Registration:
      return "Registration";
    case NetworkCommand::ConfirmationRequest:
      return "ConfirmationRequest";
    case NetworkCommand::ConfirmationResponse:
      return "ConfirmationResponse";
    case NetworkCommand::RegistrationConfirmed:
      return "RegistrationConfirmed";
    case NetworkCommand::RegistrationRefused:
      return "RegistrationRefused";
    case NetworkCommand::Ping:
      return "Ping";
    case NetworkCommand::PackInform:
      return "PackInform";
    case NetworkCommand::PackRequest:
      return "PackRequest";
    case NetworkCommand::PackRenounce:
      return "PackRenounce";
    case NetworkCommand::BlockSyncRequest:
      return "BlockSyncRequest";
    case NetworkCommand::SSRegistration:
      return "SSRegistration";
    case NetworkCommand::SSFirstRound:
      return "SSFirstRound";
    case NetworkCommand::SSRegistrationRefused:
      return "SSRegistrationRefused";
    case NetworkCommand::SSPingWhiteNode:
      return "SSPingWhiteNode";
    case NetworkCommand::SSLastBlock:
      return "SSLastBlock";
    case NetworkCommand::SSReRegistration:
      return "SSReRegistration";
    case NetworkCommand::SSSpecificBlock:
      return "SSSpecificBlock";
  }
}

class PacketFlags {
public:
  PacketFlags(const Packet& packet)
  : packet_(packet) {
  }

  std::ostream& operator()(std::ostream& os) const {
    uint8_t n = 0;
    if (packet_.isNetwork()) {
      os << "net";
      ++n;
    }
    if (packet_.isFragmented()) {
      os << (n ? "," : "") << "fragmented";
      ++n;
    }
    if (packet_.isCompressed()) {
      os << (n ? "," : "") << "compressed";
      ++n;
    }
    if (packet_.isNeighbors()) {
      os << (n ? "," : "") << "neighbors";
      ++n;
    }
    return os;
  }

private:
  const Packet& packet_;
};

std::ostream& operator<<(std::ostream& os, const PacketFlags& packetFlags) {
  return packetFlags(os);
}

std::ostream& operator<<(std::ostream& os, const Packet& packet) {
  if (!packet.isHeaderValid()) {
    os << "Invalid packet header";
    return os;
  }

  if (packet.isNetwork()) {
    const uint8_t* data = packet.getMsgData();
    size_t size = packet.getMsgSize();
    os << "Type:\t" << getNetworkCommandString(static_cast<NetworkCommand>(*data)) << "(" << int(*data) << ")"
       << std::endl;
    os << "Flags:\t" << PacketFlags(packet) << std::endl;
    os << cs::Utils::byteStreamToHex(++data, --size);
    return os;
  }

  os << "Type:\t\t" << getMsgTypesString(packet.getType()) << "(" << packet.getType() << ")" << std::endl
     << "Round:\t\t" << packet.getRoundNum() << std::endl
     << "Sender:\t\t" << cs::Utils::byteStreamToHex(packet.getSender().data(), packet.getSender().size()) << std::endl
     << "Addressee:\t" << cs::Utils::byteStreamToHex(packet.getAddressee().data(), packet.getAddressee().size())
     << std::endl
     << "Id:\t\t" << packet.getId() << std::endl
     << "Flags:\t\t" << PacketFlags(packet) << std::endl
     << cs::Utils::byteStreamToHex(packet.getMsgData(), packet.getMsgSize());
  return os;
}
