#ifndef UTILITYMESSAGE_HPP
#define UTILITYMESSAGE_HPP

#include <cstddef>
#include <cstdint>

#include <cscrypto/cscrypto.hpp>

#include <csnode/idatastream.hpp>
#include <csnode/nodecore.hpp>

#include <lib/system/common.hpp>

namespace cs {

// A utility message (MsgTypes::Utility) carries a round number, one order byte and a public key,
// signed with the blockchain key. The orders are Node::Orders, one byte on the wire.
constexpr Byte kUtilityRelease = 0;
constexpr Byte kUtilitySeal = 1;

constexpr std::size_t kUtilityPayloadSize = sizeof(RoundNumber) + sizeof(Byte) + kPublicKeyLength;

struct UtilityMessage {
    enum class Status {
        BadFormat,
        BadSignature,
        Ok
    };

    Status status = Status::BadFormat;
    RoundNumber round = 0;
    Byte order = 0;
    PublicKey key{};
};

// The signature covers the payload as it stands, so a message whose round, order or key was touched
// after signing does not verify. The length is checked first, because verifySignature reads the
// payload's data pointer, and nothing is read out of a message that does not verify; an order the
// node does not know is the caller's to handle, as before.
inline UtilityMessage readUtilityMessage(const Bytes& payload, const Signature& sig, const PublicKey& signer) {
    UtilityMessage message;
    if (payload.size() != kUtilityPayloadSize) {
        return message;
    }
    if (!cscrypto::verifySignature(sig, signer, payload.data(), payload.size())) {
        message.status = UtilityMessage::Status::BadSignature;
        return message;
    }
    IDataStream in(payload.data(), payload.size());
    in >> message.round >> message.order >> message.key;
    message.status = UtilityMessage::Status::Ok;
    return message;
}

// A signed message is an order for as long as its signature holds, and Release and Seal undo each
// other: whoever saw both could set a key's state on any node at any later time. As for a stop
// request, the other command signed with this key (Node::getNodeStopRequest), a message whose round
// is more than MaxRoundDeltaInStopRequest behind the node's own is dropped. Only old rounds are
// refused: a node behind the network sees the network's round as a future one and must still obey,
// and one that restarted and has not learned the round yet (0) takes everything, as it does now.
constexpr RoundNumber kUtilityRoundsBehind = MaxRoundDeltaInStopRequest;

inline bool isUtilityRoundInWindow(RoundNumber round, RoundNumber currentRound) {
    return round >= currentRound || currentRound - round <= kUtilityRoundsBehind;
}

}  // namespace cs

#endif  // UTILITYMESSAGE_HPP
