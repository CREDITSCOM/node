#pragma once

#include <net/transport.hpp>

namespace cs
{
    class PacketValidator
    {
    public:

        static PacketValidator& instance();

        bool validate(const Packet& pack);
        bool validate(const TaskPtr<IPacMan>& task);

        const cs::PublicKey& getStarterKey() const {
            return starter_key;
        }

    private:

        cs::PublicKey starter_key;

        PacketValidator();

        bool validateStarterSignature(const Packet& pack);

        bool validateRegistration(const Packet& pack);
        bool validateStarterRegistration(const Packet& pack);
        bool validateStopRequest(const Packet& pack);

        bool validate(NetworkCommand cmd, const Packet& pack);
        bool validate(MsgTypes msg, const Packet& pack);
    };
}
