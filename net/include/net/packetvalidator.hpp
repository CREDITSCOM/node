#ifndef PACKETVALIDATOR_HPP
#define PACKETVALIDATOR_HPP

class Packet;

namespace cs {
// validates all network packets
class PacketValidator {
public:
    static bool validate(const Packet& packet);

private:
    static bool validateNetworkPacket(const Packet& packet);
    static bool validateNodePacket(const Packet& packet);
};
}  // namespace cs

#endif // PACKETVALIDATOR_HPP
