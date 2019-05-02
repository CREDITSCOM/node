#include "nodecore.hpp"

std::size_t std::hash<cs::TransactionsPacketHash>::operator()(const cs::TransactionsPacketHash& packetHash) const noexcept {
    const std::size_t p = 16777619;
    std::size_t hash = 2166136261;

    auto data = packetHash.toBinary();
    auto size = data.size();

    for (std::size_t i = 0; i < size; ++i) {
        hash = (hash ^ data[i]) * p;
    }

    hash += hash << 13;
    hash ^= hash >> 7;
    hash += hash << 3;
    hash ^= hash >> 17;
    hash += hash << 5;

    return hash;
}
