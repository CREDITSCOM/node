#include <packet.hpp>

#include <lib/system/utils.hpp>

const char* Packet::messageTypeToString(MsgTypes messageType) {
    switch (messageType) {
        case BootstrapTable:
            return "BootstrapTable";
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

bool Packet::isHeaderValid() const {
    if (size() < getHeadersLength()) {
        cserror() << "Packet size (" << size() << ") < header length (" << getHeadersLength() << ")";
        return false;
    }

    return true;
}

std::ostream& operator<<(std::ostream& os, const Packet& packet) {
    if (!packet.isHeaderValid()) {
        os << "Invalid packet header";
        return os;
    }

    if (packet.isNetwork()) {
        const uint8_t* data = packet.getMsgData();
        os << networkCommandToString(static_cast<NetworkCommand>(*data))
           << "(" << int(*data) << "), ";
    }

    std::string flags = "Packet:\n Flags:";

    if (packet.isNetwork()) {
        flags += " network";
    }

    if (packet.isCompressed()) {
        flags += " compressed";
    }

    if (packet.isSigned()) {
        flags += " signed";
    }

    return os << flags << std::endl;
}
