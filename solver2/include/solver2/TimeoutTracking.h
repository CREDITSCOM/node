#pragma once
#include <CallsQueueScheduler.h>
#include <SolverContext.h>

namespace slv2
{

    class TimeoutTracking
    {
    public:

        void start(CallsQueueScheduler& scheduler, uint32_t wait_for_ms, const CallsQueueScheduler::ProcType& proc, bool replace_existing);

        bool cancel();

        bool is_active() const
        {
            return call_tag != CallsQueueScheduler::no_tag;
        }

    private:

        CallsQueueScheduler* pscheduler { nullptr };
        CallsQueueScheduler::CallTag call_tag { CallsQueueScheduler::no_tag };
    };

} // slv2
