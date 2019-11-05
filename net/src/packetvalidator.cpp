#include <base58.h>
#include <lib/system/logger.hpp>
#include <net/packetvalidator.hpp>
#include <net/transport.hpp>

namespace cs {
/*private*/
PacketValidator::PacketValidator() {
    starterKey_ = cs::Zero::key;
}

/*static*/
PacketValidator& PacketValidator::instance() {
    static PacketValidator inst;
    return inst;
}

bool PacketValidator::validate(const Packet& pack) {
    bool result = (pack.isHeaderValid() && (pack.getHeadersLength() < pack.size()));
    if (result) {
        if (pack.isNetwork()) {
            if (pack.isFragmented() || pack.getMsgSize() < 1) {
                result = false;
            }
            else {
                NetworkCommand cmd = (NetworkCommand) * (uint8_t*)pack.getMsgData();
                switch (cmd) {
                    case NetworkCommand::Registration:
                        result = validateRegistration(pack.getMsgSize());
                        break;
                    default:
                        break;
                }
            }
        }
        else {
            if (pack.isFragmented()) {
                if (pack.getFragmentsNum() > 1) {
                    // unable validate fragmented pack
                    result = true;
                }
                else {
                    result = validateFirstFragment(pack.getType(), pack.getMsgData(), pack.getMsgSize());
                }
            }
            else {
                result = validate(pack.getType(), pack.getMsgData(), pack.getMsgSize());
            }
        }
    }
    if (!result) {
        cswarning() << "Net: packet " << pack << " is not validated";
    }
    return result;
}

bool PacketValidator::validate(MsgTypes msg, const uint8_t* data, size_t size) {
    switch (msg) {
        case MsgTypes::BigBang:
            // still cannot validate signature, parsing required
            break;
        case MsgTypes::NodeStopRequest:
            return validateStopRequest(data, size);
        default:
            break;
    }
    return true;
}

bool PacketValidator::validateFirstFragment(MsgTypes /*msg*/, const uint8_t* /*data*/, size_t /*size*/) {
    return true;
}

bool PacketValidator::validateStarterSignature(const uint8_t* data, size_t size) {
    if (std::equal(starterKey_.cbegin(), starterKey_.cend(), cs::Zero::key.cbegin())) {
        return false;  // no SS key registered
    }
    const size_t full_data_size = size - 1;  // the 1st byte is unsigned msg type
    if (full_data_size <= cscrypto::kSignatureSize) {
        return false;  // no signed data
    }
    const size_t signed_data_size = full_data_size - cscrypto::kSignatureSize;
    const cscrypto::Byte* data_ptr = data + 1;  // the 1st byte is unsigned msg type
    const cscrypto::Byte* signature_ptr = data_ptr + signed_data_size;
    return cscrypto::verifySignature(signature_ptr, starterKey_.data(), data_ptr, signed_data_size);
}

bool PacketValidator::validateStarterRegistration(const Packet& pack) {
    if (!std::equal(starterKey_.cbegin(), starterKey_.cend(), cs::Zero::key.cbegin())) {
        // re-registration with another SS is disabled for now
        return true;
    }

    constexpr int MinPayloadSize = sizeof(MsgTypes) + cscrypto::kPublicKeySize;
    if (pack.getMsgSize() >= MinPayloadSize) {
        const uint8_t* pdata = pack.getMsgData() + 1;
        std::copy(pdata, pdata + cscrypto::kPublicKeySize, starterKey_.data());
        cslog() << "starter registration key " << cs::Utils::byteStreamToHex(starterKey_.data(), starterKey_.size()) << " ("
                << EncodeBase58(starterKey_.data(), starterKey_.data() + starterKey_.size()) << ')';
    }
    else {
        // SS key is must have
        return false;
    }

    // if size > MinRegistrationSize then Transport::parseSSSignal() will work
    // some more validation may be placed here

    return true;
}

bool PacketValidator::validateStopRequest(const uint8_t* data, size_t size) {
    constexpr size_t payload_size = sizeof(MsgTypes) + sizeof(cs::RoundNumber) + sizeof(uint16_t);  // type + rNum + version
    if (size != payload_size + cscrypto::kSignatureSize) {
        return false;  // incorrect packet size
    }
    return validateStarterSignature(data, size);
}

bool PacketValidator::validateRegistration(size_t size) {
    constexpr size_t hdr = 1 + 2 + 8;  // command Registration + version + bch_uuid
    constexpr size_t ip6 = 1 + 16;
    constexpr size_t ip4 = 1 + 4;
    constexpr size_t noip = 1;
    constexpr size_t port = 2;
    constexpr size_t id_key = sizeof(uint64_t) + sizeof(cs::PublicKey);
    constexpr size_t expected[] = {hdr + ip6 + port + id_key, hdr + ip6 + id_key, hdr + ip4 + port + id_key, hdr + ip4 + id_key, hdr + noip + port + id_key, hdr + noip + id_key};
    constexpr size_t cnt_expected = sizeof(expected) / sizeof(expected[0]);
    for (size_t i = 0; i < cnt_expected; ++i) {
        if (expected[i] == size) {
            return true;
        }
    }
    return false;
}

}  // namespace cs
