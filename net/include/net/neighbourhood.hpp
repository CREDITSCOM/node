/* Send blaming letters to @yrtimd */
#ifndef NEIGHBOURHOOD_HPP
#define NEIGHBOURHOOD_HPP

#include <memory>
#include <vector>

#include <csnode/packstream.hpp>
#include <lib/system/common.hpp>
#include <networkcommands.hpp>
#include <packet.hpp>

class Neighbourhood {
public:
    const static uint32_t MaxNeighbours = 256;
    const static uint32_t MinNeighbours = 3;
    
    void processNeighbourMessage(const cs::PublicKey& sender, const Packet&);

private:
    struct Connection {
        using Id = uint64_t;

        Id id = 0;
        cs::Version version = 0;
        cs::PublicKey key{};
        cs::Sequence lastSeq = 0;
    };

    using ConnectionPtr = std::shared_ptr<Connection>;
    using Connections = std::vector<ConnectionPtr>;

    void formRegPack(uint64_t uuid);
    void addMyOut(const uint8_t initFlagValue = 0); // to Reg Pack

    void sendRegistrationRequest();
    void sendRegistrationConfirmation();
    void sendRegistrationRefusal(const RegistrationRefuseReasons reason);
    void sendPingPack();

    bool gotRegistrationRequest();
    bool gotRegistrationConfirmation();
    bool gotRegistrationRefusal();
    bool gotPing();

    Packet regPack_;
    cs::IPackStream iPackStream_;
    Connections neighbours_;
};
#endif  // NEIGHBOURHOOD_HPP
