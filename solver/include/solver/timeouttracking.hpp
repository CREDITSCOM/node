#pragma once

#include <callsqueuescheduler.hpp>

namespace cs {

class TimeoutTracking {
public:

  void start(CallsQueueScheduler& scheduler, uint32_t wait_for_ms, const CallsQueueScheduler::ProcType& proc,
             bool replace_existing, CallsQueueScheduler::CallTag tag = CallsQueueScheduler::auto_tag);

  bool cancel();

  bool is_active() const {
    return call_tag != CallsQueueScheduler::no_tag;
  }

private:
  CallsQueueScheduler* pscheduler{nullptr};
  CallsQueueScheduler::CallTag call_tag{CallsQueueScheduler::no_tag};
};

}  // namespace slv2
