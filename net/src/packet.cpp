#include <lz4.h>

#include <lib/system/utils.hpp>
#include "packet.hpp"
#include "transport.hpp"  // for NetworkCommand

RegionAllocator Message::allocator_;

enum Lengths {
    FragmentedHeader = 36
};

const char* Packet::messageTypeToString(MsgTypes messageType) {
    switch (messageType) {
        case RoundTableSS:
            return "RoundTableSS";
        case Transactions:
            return "Transactions";
        case FirstTransaction:
            return "FirstTransaction";
        case NewBlock:
            return "NewBlock";
        case BlockHash:
            return "BlockHash";
        case BlockRequest:
            return "BlockRequest";
        case RequestedBlock:
            return "RequestedBlock";
        case FirstStage:
            return "FirstStage";
        case SecondStage:
            return "SecondStage";
        case ThirdStage:
            return "ThirdStage";
        case FirstStageRequest:
            return "FirstStageRequest";
        case SecondStageRequest:
            return "SecondStageRequest";
        case ThirdStageRequest:
            return "ThirdStageRequest";
        case RoundTableRequest:
            return "RoundTableRequest";
        case RoundTableReply:
            return "RoundTableReply";
        case TransactionPacket:
            return "TransactionPacket";
        case TransactionsPacketRequest:
            return "TransactionsPacketRequest";
        case TransactionsPacketReply:
            return "TransactionsPacketReply";
        case NewCharacteristic:
            return "NewCharacteristic";
        case WriterNotification:
            return "WriterNotification";
        case FirstSmartStage:
            return "FirstSmartStage";
        case SecondSmartStage:
            return "SecondSmartStage";
        case RoundTable:
            return "RoundTable";
        case ThirdSmartStage:
            return "ThirdSmartStage";
        case SmartFirstStageRequest:
            return "SmartFirstStageRequest";
        case SmartSecondStageRequest:
            return "SmartSecondStageRequest";
        case SmartThirdStageRequest:
            return "SmartThirdStageRequest";
        case HashReply:
            return "HashReply";
        case BigBang:
            return "BigBang";
        case NodeStopRequest:
            return "NodeStopRequest";
        case RejectedContracts:
            return "RejectedContracts";
        default:
            return "Unknown";
    }
}

const cs::Hash& Packet::getHeaderHash() const {
    if (!headerHashed_) {
        headerHash_ = generateHash(static_cast<const char*>(data_->get()) + Offsets::FragmentsNum, Lengths::FragmentedHeader);
        headerHashed_ = true;
    }
    return headerHash_;
}

bool Packet::isHeaderValid() const {
    if (isFragmented()) {
        if (isNetwork()) {
            cserror() << "Network packet is fragmented";
            return false;
        }

        const auto& frNum = getFragmentsNum();
        const auto& frId = getFragmentId();

        if (!hasValidFragmentation()) {
            cserror() << "Packet " << Packet::messageTypeToString(this->getType()) << " has invalid header: frId(" << frId << ") >= frNum(" << frNum << ")";
            return false;
        }
    }

    if (size() <= getHeadersLength()) {
        cserror() << "Packet size (" << size() << ") <= header length (" << getHeadersLength() << ")" << (isNetwork() ? ", network" : "") << (isFragmented() ? ", fragmeted" : "")
                  << ", type " << Packet::messageTypeToString(getType()) << "(" << static_cast<int>(getType()) << ")";
        return false;
    }

    return true;
}

uint32_t Packet::getHeadersLength() const {
    if (!headersLength_) {
        headersLength_ = calculateHeadersLength();
    }

    return headersLength_;
}

uint32_t Packet::calculateHeadersLength() const {
    uint32_t length = sizeof(BaseFlags);  // Flags

    if (isFragmented()) {
        length += sizeof(uint16_t) + sizeof(uint16_t);  // Min fragments & all fragments
    }

    if (!isNetwork()) {
        length += kPublicKeyLength + sizeof(getId());  // Sender key + ID

        if (!isBroadcast() && !isNeighbors()) {
            length += kPublicKeyLength;  // Receiver key
        }
    }

    return length;
}

void Packet::recalculateHeadersLength() {
    headersLength_ = calculateHeadersLength();
}

MessagePtr PacketCollector::getMessage(const Packet& pack, bool& newFragmentedMsg) {
    if (!pack.isFragmented()) {
        return MessagePtr();
    }
    
    if (!pack.hasValidFragmentation()) {
        return MessagePtr();
    }

    newFragmentedMsg = false;

    MessagePtr* msgPtr;
    MessagePtr msg;

    {
        cs::Lock l(mLock_);
        msgPtr = &map_.tryStore(pack.getHeaderHash());
    }

    if (!*msgPtr) {  // First time
        *msgPtr = msg = msgAllocator_.emplace();
        msg->packetsLeft_ = pack.getFragmentsNum();
        msg->packetsTotal_ = pack.getFragmentsNum();
        msg->headerHash_ = pack.getHeaderHash();
        // to ensure not to contain dirty fragments in buffer (prevent goodPlace below from to be incorrect):
        msg->clearFragments();
        newFragmentedMsg = true;
    }
    else {
        msg = *msgPtr;
    }

    {
        cs::Lock lock(msg->pLock_);
        auto goodPlace = msg->packets_ + pack.getFragmentId(); // valid fragmentation has already been tested

        if (!*goodPlace) {
            msg->maxFragment_ = std::max(pack.getFragmentsNum(), msg->maxFragment_);
            --msg->packetsLeft_;
            *goodPlace = pack;
        }

        if (msg->packetsTotal_ >= 20) {
            if (msg->packetsLeft_ != 0) {
                // the 1st fragment contains full info:
                if (pack.getFragmentId() == 0) {
                    csdetails() << "COLLECT> recv pack " << Packet::messageTypeToString(pack.getType()) << " of " << msg->packetsTotal_ << ", round " << pack.getRoundNum();
                }
                csdetails() << "COLLECT> ready " << msg->packetsTotal_ - msg->packetsLeft_ << " / " << msg->packetsTotal_;
            }
            else {
                csdetails() << "COLLECT> done (" << msg->packetsTotal_ << ") " << Packet::messageTypeToString(msg->getFirstPack().getType()) << ", round "
                            << msg->getFirstPack().getRoundNum();
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

        for (uint32_t i = 0; i < packetsTotal_; ++i, ++pack) {
            totalSize += static_cast<uint32_t>((pack->size() - headersLength));
        }

        fullData_ = allocator_.allocateNext(totalSize);

        uint8_t* data = static_cast<uint8_t*>(fullData_->get());
        pack = packets_;

        for (uint32_t i = 0; i < packetsTotal_; ++i, ++pack) {
            uint32_t headerSize = static_cast<uint32_t>((i == 0) ? 0 : headersLength);

            uint32_t cSize = cs::numeric_cast<uint32_t>(pack->size()) - headerSize;
            auto source = (reinterpret_cast<const char*>(pack->data())) + headerSize;

            std::copy(source, source + cSize, data);
            data += cSize;
        }
    }
}

// scans array of future fragments and clears all dirty elements, scans all elements
size_t Message::clearBuffer(size_t from, size_t to) {
    if (to <= from || to >= Packet::MaxFragments) {
        return 0;
    }
    size_t cnt = 0;
    for (size_t i = from; i < to; i++) {
        // Packet's operator bool redirects call to Packet::data_::operator bool
        if (packets_[i]) {
            csdebug() << "Net: potential heap corruption prevented in message.packets_[" << i << "]";
            packets_[i] = Packet{};
            ++cnt;
        }
    }
    return cnt;
}

Message::~Message() {
    /*auto pEnd = packets_ + packetsTotal_;
    for (auto ptr = packets_; ptr != pEnd; ++ptr)
      if (ptr) ptr->~Packet();

      memset(packets_, 0, sizeof(Packet*) * packetsTotal_);*/

    //DEBUG: prevent corruption after heap is damaged,
    // assume maxFragment "points" behind the last fragment,
    // idea is to avoid call to MemPtr<> destructor on incorrect object
    size_t cnt = clearUnused();
    if (cnt > 0) {
        csdebug() << "Net: memory corruption prevented, invalid fragments (" << cnt << ") is behind the max of " << maxFragment_ << " and cannot been destructed";
        Transport::cntCorruptedFragments += cnt;
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
            os << (n ? ", " : "") << "fragmented(" << packet_.getFragmentsNum() << ")";
            ++n;
        }

        if (packet_.isCompressed()) {
            os << (n ? ", " : "") << "compressed";
            ++n;
        }

        if (packet_.isNeighbors()) {
            os << (n ? ", " : "") << "neighbors";
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
        os << Transport::networkCommandToString(static_cast<NetworkCommand>(*data)) << "(" << int(*data) << "), ";
        os << "flags: " << PacketFlags(packet);
        return os;
    }

    if (packet.isFragmented()) {
        if (packet.getFragmentId() == 0) {
            os << Packet::messageTypeToString(packet.getType()) << "(" << packet.getType() << "), ";
            os << "round " << packet.getRoundNum() << ", ";
        }
        else {
            os << "fragment id: " << packet.getFragmentId() << ", ";
        }
    }

    os << "flags: " << PacketFlags(packet);
    os << ", id: " << packet.getId() << std::endl;
    os << "Sender:\t\t" << cs::Utils::byteStreamToHex(packet.getSender().data(), packet.getSender().size());

    if (!packet.isBroadcast()) {
        os << std::endl;
        os << "Addressee:\t" << cs::Utils::byteStreamToHex(packet.getAddressee().data(), packet.getAddressee().size());
    }

    return os;
}
