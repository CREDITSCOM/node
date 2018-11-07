#pragma once

#include <gmock/gmock.h>
#include <CallsQueueScheduler.h>

namespace slv2
{
    class TimeoutTracking
    {
    public:

        TimeoutTracking() = default;

        TimeoutTracking(const TimeoutTracking&)
        {}

        MOCK_METHOD4(start, void(CallsQueueScheduler&, size_t, CallsQueueScheduler::ProcType, bool));
        MOCK_METHOD0(cancel, bool());
    };
}