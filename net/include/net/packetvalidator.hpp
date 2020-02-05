#ifndef PACKETVALIDATOR_HPP
#define PACKETVALIDATOR_HPP

#include <net/transport.hpp>

namespace cs {
class PacketValidator {
public:
    static PacketValidator& instance();

    bool validate(const Packet& pack);
    bool validate(const Message& msg);

    const cs::PublicKey& getStarterKey() const {
        return starterKey_;
    }

private:
    cs::PublicKey starterKey_;
    // indicates the last registration on starter result: success or failure
    bool last_ss_reg_failed;

    PacketValidator();

    bool validate(MsgTypes msg, const uint8_t* data, size_t size);
    bool validateFirstFragment(MsgTypes msg, const uint8_t* data, size_t size);

    bool validateStarterSignature(const uint8_t* data, size_t size);

    bool validateRegistration(size_t size);
    bool validateStarterRegistration(const Packet& pack);
    bool validateStopRequest(const uint8_t* data, size_t size);
};
}  // namespace cs

#endif // PACKETVALIDATOR_HPP
