#include <lz4.h>

#include <lib/system/utils.hpp>
#include "packet.hpp"
#include "transport.hpp"  // for NetworkCommand

// RegionAllocator Message::allocator_;

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
        case RoundPackRequest:
            return "RoundPackRequest";
        case StateRequest:
            return "StateRequest";
        case StateReply:
            return "StateReply";
        case EmptyRoundPack:
            return "EmptyRoundPack";
        default:
            return "Unknown";
    }
}

const cs::Hash& Packet::getHeaderHash() const {
    if (!headerHashed_) {
        headerHash_ = generateHash(region_.data() + Offsets::FragmentsNum, Lengths::FragmentedHeader);
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

        if (!isBroadcast() && !isDirect()) {
            length += kPublicKeyLength;  // Receiver key
        }
    }

    return length;
}

void Packet::recalculateHeadersLength() {
    headersLength_ = calculateHeadersLength();
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

        if (packet_.isDirect()) {
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
