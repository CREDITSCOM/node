#pragma once
#include <gmock/gmock.h>

namespace cs
{
    class WalletsState
    {
    public:
        
        MOCK_METHOD0(updateFromSource, void());
    };
}