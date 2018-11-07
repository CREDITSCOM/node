#pragma once

#include <array>

namespace Credits
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
