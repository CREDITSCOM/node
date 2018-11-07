#pragma once

namespace slv2
{
    class CallsQueueScheduler
    {
    public:
        using ProcType = std::function<void()>;
        using CallTag = uintptr_t;

        CallsQueueScheduler() = default;

        CallsQueueScheduler(const CallsQueueScheduler&)
        {}

        constexpr static CallTag no_tag = 0;

    };
}