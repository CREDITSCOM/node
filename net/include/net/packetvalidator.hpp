#ifndef PACKETVALIDATOR_HPP
#define PACKETVALIDATOR_HPP

#include <net/transport.hpp>

namespace cs {
class PacketValidator {
public:
    static PacketValidator& instance();

    bool validate(const Packet& pack);

    const cs::PublicKey& getStarterKey() const {
        return starterKey_;
    }

private:
    cs::PublicKey starterKey_;

    PacketValidator();
};
}  // namespace cs

#endif // PACKETVALIDATOR_HPP
