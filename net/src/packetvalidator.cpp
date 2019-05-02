#include <net/packetvalidator.hpp>
#include <net/transport.hpp>

namespace cs
{

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
        return true;
    }

    bool PacketValidator::validate(const TaskPtr<IPacMan>& task)
    {
        return validate(task->pack);
    }

    bool PacketValidator::validate(NetworkCommand cmd, const Packet& pack) {
        switch (cmd) {
        case NetworkCommand::Registration:
            return validateRegistration(pack);
        default:
            break;
        }
        return true;
    }

    bool PacketValidator::validateRegistration(const Packet& pack) {
        constexpr size_t ver = 1 + 1; // command Registration + version
        constexpr size_t ip6 = 1 + 16;
        constexpr size_t ip4 = 1 + 4;
        constexpr size_t noip = 1;
        constexpr size_t port = 2;
        constexpr size_t id_key = sizeof(uint64_t) + sizeof(cs::PublicKey);
        constexpr size_t expected[] = {
            ver + ip6 + port + id_key,
            ver + ip6 + id_key,
            ver + ip4 + port + id_key,
            ver + ip4 + id_key,
            ver + noip + port + id_key,
            ver + noip + id_key
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
