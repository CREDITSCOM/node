#include "nodecore.hpp"

namespace cs
{
    /*static*/
    cs::Hash Zero::hash;

    /*static*/
    cs::Signature Zero::signature;

    /*static*/
    cs::PublicKey Zero::key;

    Zero::Zero()
    {
        std::fill(hash.begin(), hash.end(), '\0');
        std::fill(signature.begin(), signature.end(), '\0');
        std::fill(key.begin(), key.end(), '\0');
    }

} // cs

namespace
{
    // the only purpose of the object is to init static members
    cs::Zero zero;
}

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
