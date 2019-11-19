#include <lib/system/common.hpp>

size_t std::hash<cs::PublicKey>::operator()(const cs::PublicKey& key) const {
    static_assert(sizeof(size_t) < sizeof(cs::PublicKey));

    size_t res;
    std::copy(key.data(), key.data() + sizeof(res), reinterpret_cast<uint8_t*>(&res));

    return res;
}
