#pragma once

#include <net/transport.hpp>

namespace cs
{
    class PacketValidator
    {
    public:

        static PacketValidator& instance();

        bool validate(const Packet& pack);
        bool validate(const Message& msg);

        const cs::PublicKey& getStarterKey() const {
            return starter_key;
        }

    private:

        cs::PublicKey starter_key;

        PacketValidator();

        bool validate(MsgTypes msg, const uint8_t* data, size_t size);

        bool validateStarterSignature(const uint8_t* data, size_t size);

        bool validateRegistration(size_t size);
        bool validateStarterRegistration(const Packet& pack);
        bool validateStopRequest(const uint8_t* data, size_t size);

    };
}
