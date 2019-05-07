#include <net/packetvalidator.hpp>
#include <net/transport.hpp>
#include <lib/system/logger.hpp>
#include <base58.h>

namespace cs
{
    /*private*/
    PacketValidator::PacketValidator() {
        starter_key = cs::Zero::key;
    }

    /*static*/
    PacketValidator& PacketValidator::instance() {
        static PacketValidator inst;
        return inst;
    }

    bool PacketValidator::validate(const Packet& pack)
    {
        if (!pack.isHeaderValid()) {
            return false;
        }
        if (pack.getHeadersLength() >= pack.size()) {
            return false;
        }
        if (pack.isNetwork()) {
            if (pack.isFragmented()) {
                return false;
            }
            if (pack.getMsgSize() < 1) {
                return false;
            }
            NetworkCommand cmd = (NetworkCommand) * (uint8_t*)pack.getMsgData();
            return validate(cmd, pack);
        }
        MsgTypes msg = (MsgTypes) * (uint8_t*)pack.getMsgData();
        return validate(msg, pack);
    }

    bool PacketValidator::validate(const TaskPtr<IPacMan>& task)
    {
        return validate(task->pack);
    }

    bool PacketValidator::validate(NetworkCommand cmd, const Packet& pack) {
        switch (cmd) {
        case NetworkCommand::Registration:
            return validateRegistration(pack);
        case NetworkCommand::SSRegistration:
            validateStarterRegistration(pack);
            break;
        default:
            break;
        }
        return true;
    }

    bool PacketValidator::validate(MsgTypes msg, const Packet& pack) {
        switch (msg) {
        case MsgTypes::BigBang:
            // still cannot validate signature, parsing required
            break;
        case MsgTypes::NodeStopRequest:
            return validateStopRequest(pack);
        default:
            break;
        }
        return true;
    }

    bool PacketValidator::validateStarterSignature(const Packet& pack) {
        if (std::equal(starter_key.cbegin(), starter_key.cend(), cs::Zero::key.cbegin())) {
            return false; // no SS key registered
        }
        const size_t full_data_size = pack.getMsgSize() - 1; // the 1st byte is unsigned msg type
        if (full_data_size <= cscrypto::kSignatureSize) {
            return false; // no signed data
        }
        const size_t signed_data_size = full_data_size - cscrypto::kSignatureSize;
        const cscrypto::Byte* data_ptr = pack.getMsgData() + 1; // the 1st byte is unsigned msg type
        const cscrypto::Byte* signature_ptr = data_ptr + signed_data_size;
        return cscrypto::verifySignature(signature_ptr, starter_key.data(), data_ptr, signed_data_size);
    }

    bool PacketValidator::validateStarterRegistration(const Packet& pack) {
        if (! std::equal(starter_key.cbegin(), starter_key.cend(), cs::Zero::key.cbegin())) {
            // re-registration with another SS is disabled for now
            return false;
        }

        constexpr int MinPayloadSize = sizeof(MsgTypes) + cscrypto::kPublicKeySize;
        if (pack.getMsgSize() >= MinPayloadSize) {
            const uint8_t* pdata = pack.getMsgData() + 1;
            std::copy(pdata, pdata + cscrypto::kPublicKeySize, starter_key.data());
            cslog() << "starter registration key " << cs::Utils::byteStreamToHex(starter_key.data(), starter_key.size())
                << " (" << EncodeBase58(starter_key.data(), starter_key.data() + starter_key.size()) << ')';
        }
        else {
            // SS key is must have
            return false;
        }

        // if size > MinRegistrationSize then Transport::parseSSSignal() will work
        // some more validation may be placed here

        return true;
    }

    bool PacketValidator::validateStopRequest(const Packet& pack) {
        constexpr size_t payload_size = sizeof(MsgTypes) + sizeof(cs::RoundNumber) + sizeof(uint16_t); // type + rNum + version
        if (pack.getMsgSize() != payload_size + cscrypto::kSignatureSize) {
            return false; // incorrect packet size
        }
        return validateStarterSignature(pack);
    }

    bool PacketValidator::validateRegistration(const Packet& pack) {
        constexpr size_t hdr = 1 + 2; // command Registration + version
        constexpr size_t ip6 = 1 + 16;
        constexpr size_t ip4 = 1 + 4;
        constexpr size_t noip = 1;
        constexpr size_t port = 2;
        constexpr size_t id_key = sizeof(uint64_t) + sizeof(cs::PublicKey);
        constexpr size_t expected[] = {
            hdr + ip6 + port + id_key,
            hdr + ip6 + id_key,
            hdr + ip4 + port + id_key,
            hdr + ip4 + id_key,
            hdr + noip + port + id_key,
            hdr + noip + id_key
        };
        constexpr size_t cnt_expected = sizeof(expected) / sizeof(expected[0]);
        const size_t len = pack.getMsgSize();
        for (size_t i = 0; i < cnt_expected; ++i) {
            if (expected[i] == len) {
                return true;
            }
        }
        return false;
    }

} // cs
