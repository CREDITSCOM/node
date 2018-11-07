#include <TimeoutTracking.h>

namespace slv2
{

    void TimeoutTracking::start(CallsQueueScheduler& scheduler, uint32_t wait_for_ms, const CallsQueueScheduler::ProcType & proc, bool replace_existing)
    {
        pscheduler = &scheduler;
        call_tag = pscheduler->InsertOnce(
            wait_for_ms,
            [this, proc]() {
                call_tag = CallsQueueScheduler::no_tag;
                proc();
            },
            replace_existing);
    }

    bool TimeoutTracking::cancel()
    {
        if(call_tag != CallsQueueScheduler::no_tag) {
            pscheduler->Remove(call_tag);
            call_tag = CallsQueueScheduler::no_tag;
            return true;
        }
        return false;
    }

} // slv2
