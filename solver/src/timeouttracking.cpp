#include <timeouttracking.hpp>

namespace cs {

void TimeoutTracking::start(CallsQueueScheduler& scheduler, uint32_t wait_for_ms,
                            const CallsQueueScheduler::ProcType& proc, bool replace_existing) {
  pscheduler = &scheduler;
  call_tag = pscheduler->InsertOnce(wait_for_ms,
                                    [this, proc]() {
                                      // extra test whether to execute proc():
                                      if (call_tag != CallsQueueScheduler::no_tag) {
                                        call_tag = CallsQueueScheduler::no_tag;
                                        proc();
                                      }
                                    },
                                    replace_existing);
}

bool TimeoutTracking::cancel() {
  if (call_tag != CallsQueueScheduler::no_tag) {
    bool ret = pscheduler->Remove(call_tag);
    call_tag = CallsQueueScheduler::no_tag;
    return ret;
  }
  return false;
}

}  // namespace slv2
