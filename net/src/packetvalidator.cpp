#include <base58.h>

#include <lib/system/logger.hpp>

#include <net/packetvalidator.hpp>
#include <net/transport.hpp>

#include <csnode/conveyer.hpp>

// you may add special packet validation to special type
// it may be network command type or node type messages

namespace cs {

namespace {
std::string bcStr("81p93jgrHqA9L4Vkdut9ESSCV1XNoge7LXBW96cuA7sm");

struct BlockChainKey {
  BlockChainKey() {
      cs::Bytes keyBytes;
      DecodeBase58(bcStr, keyBytes);
      std::copy(keyBytes.begin(), keyBytes.end(), key_.begin());
  }

  cs::PublicKey key_{};
};
} // namespace

constexpr static cs::RoundNumber packetTypeRoundTimeout(const MsgTypes type) {
    switch (type) {
        case MsgTypes::FirstSmartStage:
        case MsgTypes::SecondSmartStage:
        case MsgTypes::ThirdSmartStage:
        case MsgTypes::RejectedContracts:
            return 100;
        case MsgTypes::TransactionPacket:
        case MsgTypes::TransactionsPacketRequest:
        case MsgTypes::TransactionsPacketReply:
            return cs::Conveyer::MetaCapacity;
        default:
            return 5;
    }
}

bool PacketValidator::validate(const Packet& packet) {
    if (!packet.isHeaderValid()) {
        cswarning() << "Packet header " << packet << " is not validated";
        return false;
    }

    if (packet.isNetwork()) {
        return validateNetworkPacket(packet);
    }

    return validateNodePacket(packet);
}

const cs::PublicKey& PacketValidator::getBlockChainKey() {
    static BlockChainKey bcKey;
    return bcKey.key_;
}

bool PacketValidator::validateNetworkPacket(const Packet&) {
    return true;
}

bool PacketValidator::validateNodePacket(const Packet& packet) {
    auto round = packet.getRoundNum();
    auto messageType = packet.getType();
    auto size = packet.size();

    // zero size packets && never cut packets
    switch (messageType) {
        case MsgTypes::RoundTableRequest:  // old-round node may ask for round info
            return true;
        default:
            break;
    }

    // all other zero size packets cut
    if (size == 0) {
        cserror() << "Bad packet size of type " << Packet::messageTypeToString(messageType) << ", why is it zero?";
        return false;
    }

    // never cut packets
    switch (messageType) {
        case MsgTypes::BlockRequest:
        case MsgTypes::RequestedBlock:
        case MsgTypes::Utility:
        case MsgTypes::NodeStopRequest:
        case MsgTypes::RoundTable:
        case MsgTypes::BootstrapTable:
            return true;
        default:
            break;
    }

    // cut slow packs
    if ((round + packetTypeRoundTimeout(messageType)) < cs::Conveyer::instance().currentRoundNumber()) {
        csdebug() << "TRANSPORT> Ignore old packs, round " << round << ", type " << Packet::messageTypeToString(messageType);
        return false;
    }

    // packets which validator may cut
    switch (messageType) {
        case MsgTypes::BlockHash:
        case MsgTypes::HashReply:
        case MsgTypes::TransactionPacket:
        case MsgTypes::TransactionsPacketRequest:
        case MsgTypes::TransactionsPacketReply:
        case MsgTypes::FirstStage:
        case MsgTypes::SecondStage:
        case MsgTypes::FirstStageRequest:
        case MsgTypes::SecondStageRequest:
        case MsgTypes::ThirdStageRequest:
        case MsgTypes::ThirdStage:
        case MsgTypes::FirstSmartStage:
        case MsgTypes::SecondSmartStage:
        case MsgTypes::ThirdSmartStage:
        case MsgTypes::SmartFirstStageRequest:
        case MsgTypes::SmartSecondStageRequest:
        case MsgTypes::SmartThirdStageRequest:
        case MsgTypes::RejectedContracts:
        case MsgTypes::RoundTableReply:
        case MsgTypes::RoundPackRequest:
        case MsgTypes::EmptyRoundPack:
        case MsgTypes::StateRequest:
        case MsgTypes::StateReply:
        case MsgTypes::BlockAlarm:
        case MsgTypes::EventReport:
            return true;

        default: {
            cswarning() << "Packet validator> Unknown message type " << Packet::messageTypeToString(messageType) << " pack round " << round;
            return false;
        }
    }
}
}  // namespace cs
