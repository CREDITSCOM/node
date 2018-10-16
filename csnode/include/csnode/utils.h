#ifndef CSNODE_UTILS_H
#define CSNODE_UTILS_H

#include <array>
#include <ostream>

namespace cs
{
    enum class Direction : uint8_t
    {
        PrevBlock,
        NextBlock
    };
    std::ostream& operator<<(std::ostream& os, Direction dir);

    std::ostream& printHex(std::ostream& os, const char* bytes, size_t num);

    template<typename T, size_t Size>
    std::ostream& operator<<(std::ostream& os, const std::array<T, Size>& address)
    {
        printHex(os, reinterpret_cast<const char*>(&*address.begin()), address.size());
        return os;
    }
}

#endif
