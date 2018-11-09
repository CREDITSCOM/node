#pragma once

#include <array>

namespace cs
{
    class WalletsCache
    {
    public:

        struct WalletData
        {
            using Address = std::array<uint8_t, 32>;
            Address address_;
        };
    };
}
