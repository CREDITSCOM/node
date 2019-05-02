#pragma once

#include <net/transport.hpp>
//class Packet;
//class IPacMan;
//template <typename pacman> class TaskPtr;

namespace cs
{
    class PacketValidator
    {
    public:

        static PacketValidator& instance();


        bool validate(const Packet& pack);
        bool validate(const TaskPtr<IPacMan>& task);

    private:

        bool validateRegistration(const Packet& pack);

        bool validate(NetworkCommand cmd, const Packet& pack);
    };
}
