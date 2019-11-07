/* Send blaming letters to @yrtimd */
#ifndef NEIGHBOURHOOD_HPP
#define NEIGHBOURHOOD_HPP

#include <memory>
#include <vector>

#include <lib/system/common.hpp>
#include <networkcommands.hpp>
#include <packet.hpp>

class Neighbourhood {
public:
    const static uint32_t MaxNeighbours = 256;
    const static uint32_t MinNeighbours = 3;
    
    void processNeighbourMessage(const cs::PublicKey&, const Packet&) {}

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

    Connections neighbours_;
};
#endif  // NEIGHBOURHOOD_HPP
