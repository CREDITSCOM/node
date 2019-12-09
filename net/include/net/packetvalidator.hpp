#ifndef PACKETVALIDATOR_HPP
#define PACKETVALIDATOR_HPP

class Packet;

namespace cs {
// validates all network packets
class PacketValidator {
public:
    static bool validate(const Packet& packet);
};
}  // namespace cs

#endif // PACKETVALIDATOR_HPP
