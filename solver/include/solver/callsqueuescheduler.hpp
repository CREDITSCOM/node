#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <thread>

// template<typename TResol = std::chrono::milliseconds>
class CallsQueueScheduler {
public:
  enum class Launch
  {

    ///< An enum constant representing the once option: launch once, cancel if previous schedule call still is not done
    once,

    ///< An enum constant representing the periodic option: launch periodically, schedule call cancels if already
    ///< running one cycle
    periodic
  };

  using ClockType = std::chrono::steady_clock;
  using ProcType = std::function<void()>;
  using CallTag = uintptr_t;

  constexpr static CallTag no_tag = 0;

  /**
   * @fn  CallsQueueScheduler::CallsQueueScheduler()
   *
   * @brief   Default constructor
   *
   * @author  aae
   * @date    17.09.2018
   */

  CallsQueueScheduler()
  : _queue(compare) {
  }

  CallsQueueScheduler(const CallsQueueScheduler&) = delete;
  CallsQueueScheduler& operator=(const CallsQueueScheduler&) = delete;

  /**
   * @fn  void CallsQueueScheduler::Run();
   *
   * @brief   Runs scheduler in separate thread, is optional for call. Would be automatically called by the first
   * Insert() operation
   *
   * @author  aae
   * @date    17.09.2018
   */

  void Run();

  /**
   * @fn  void CallsQueueScheduler::Stop();
   *
   * @brief   Stops this object by clearing the queue and waits for worker thread to stop
   *
   * @author  aae
   * @date    17.09.2018
   */

  void Stop();

  /**
   * @fn  CallTag CallsQueueScheduler::Insert(ClockType::duration wait_for, const ProcType& proc, Launch scheme, bool
   * replace_existing = false);
   *
   * @brief   Inserts new call into queue according to wait_for parameter. Do check before insert
   *          to avoid queuing of duplicated calls
   *
   * @author  aae
   * @date    17.09.2018
   *
   * @param   wait_for            Time to wait before do call a procedure.
   * @param   proc                The procedure to be scheduled for call.
   * @param   scheme              The scheme: once - do one call, periodic - repeat calls every
   *                              wait_for period.
   * @param   replace_existing    (Optional) True to replace existing scheduled calls if any, otherwise reject new
   * schedule.
   *
   * @return  A tag that can be used in future to remove scheduled call from queue if any, or
   *          CallsQueueScheduler::no_tag if schedule failed. If schedule rejected due to existing one returns tag of
   * existing schedule.
   */

  CallTag Insert(ClockType::duration wait_for, const ProcType& proc, Launch scheme, bool replace_existing = false);

  /**
   * @fn  CallTag CallsQueueScheduler::InsertOnce(uint32_t wait_for_ms, const ProcType& proc, bool replace_existing =
   * false)
   *
   * @brief   Schedule proc to be called once
   *
   * @author  aae
   * @date    18.09.2018
   *
   * @param   wait_for_ms         The wait for in milliseconds.
   * @param   proc                The procedure to call.
   * @param   replace_existing    (Optional) True to replace existing scheduled calls if any, otherwise reject new
   * schedule.
   *
   * @return  An id that can be used in future to remove scheduled call from queue if any, or
   *          CallsQueueScheduler::no_tag if schedule failed. If schedule rejected due to existing one returns tag of
   * existing schedule.
   */

  CallTag InsertOnce(uint32_t wait_for_ms, const ProcType& proc, bool replace_existing = false) {
    return Insert(std::chrono::milliseconds(wait_for_ms), proc, Launch::once, replace_existing);
  }

  /**
   * @fn  CallTag CallsQueueScheduler::InsertPeriodic(uint32_t wait_for_ms, const ProcType& proc, bool replace_existing
   * = false)
   *
   * @brief   Schedule periodic call of proc
   *
   * @author  aae
   * @date    18.09.2018
   *
   * @param   wait_for_ms         The wait for the first call in milliseconds and period between
   *                              calls.
   * @param   proc                The procedure o call.
   * @param   replace_existing    (Optional) True to replace existing scheduled calls if any, otherwise reject new
   * schedule.
   *
   * @return  An id that can be used in future to remove scheduled call from queue if any, or
   *          CallsQueueScheduler::no_tag if schedule failed. If schedule rejected due to existing one returns tag of
   * existing schedule.
   */

  CallTag InsertPeriodic(uint32_t wait_for_ms, const ProcType& proc, bool replace_existing = false) {
    return Insert(std::chrono::milliseconds(wait_for_ms), proc, Launch::periodic, replace_existing);
  }

  /**
   * @fn  bool CallsQueueScheduler::Remove(CallTag id);
   *
   * @brief   Removes the scheduled call idenified by id.
   *
   * @author  aae
   * @date    17.09.2018
   *
   * @param   id  The identifier of call to remove. The id must be obtained by previous call to Insert()
   *
   * @return  True if it succeeds, false if it fails.
   */

  bool Remove(CallTag id);

  /**
   * @fn  void RemoveAll();
   *
   * @brief   Removes all scheduled calls from queue. Do not concern already initiated calls placed to CallsQueue
   *
   * @author  aae
   * @date    18.09.2018
   */

  void RemoveAll();

  /**
   * @fn  void CallsQueueScheduler::Clear();
   *
   * @brief   Clears the queue of scheduled calls
   *
   * @author  aae
   * @date    17.09.2018
   */

  void Clear();

  /**
   * @fn  uint32_t CallsQueueScheduler::TotalExecutedCalls() const;
   *
   * @brief   Total executed calls
   *
   * @author  aae
   * @date    17.09.2018
   *
   * @return  The total number of executed calls during work.
   */

  uint32_t TotalExecutedCalls() const {
    return _cnt_total;
  }

  /**
   * @fn  uint32_t CallsQueueScheduler::TotalBlockedOnQueue() const
   *
   * @brief   Total blocked on queue
   *
   * @author  aae
   * @date    17.09.2018
   *
   * @return  The total number of calls blocked on queue.
   */

  uint32_t TotalBlockedOnQueue() const {
    return _cnt_block_que;
  }

  /**
   * @fn  uint32_t CallsQueueScheduler::TotalBlockedOnExecute() const
   *
   * @brief   Total blocked on execute
   *
   * @author  aae
   * @date    17.09.2018
   *
   * @return  The total number of calls blocked on execute.
   */

  uint32_t TotalBlockedOnExecute() const {
    return _cnt_block_exe;
  }

private:
  /**
   * @struct  Context
   *
   * @brief   Stores all info to call, re-schedule and cancel further calls in queue.
   *
   * @author  aae
   * @date    17.09.2018
   */

  struct Context {
    /** @brief   The identifier: lets find item in queue (e.g. for remove) */
    CallTag id;

    /** @brief   The time point for scheduled execution */
    ClockType::time_point tp;

    /** @brief   The delta - time period for periodic calls */
    long long dt;

    /** @brief   The procedure to call*/
    ProcType proc;

    // support for std::find() by id
    bool operator==(const CallTag rhs) const {
      return id == rhs;
    }
  };

  // used for sort items in _queue by time to call
  std::function<bool(const Context& lhs, const Context& rhs)> compare = [](const Context& lhs, const Context& rhs) {
    return lhs.tp < rhs.tp;
  };

  // container requirements: auto sort by tp, search by id
  std::multiset<Context, decltype(compare)> _queue;
  // sync access to _queue
  std::mutex _mtx_queue;

  // process _queue and puts on time calls into CallsQueue::instance() object
  std::thread _worker;

  // signals to _worker thread that _queue was updated
  std::condition_variable _signal;
  // need by _signal
  std::mutex _mtx_signal;

  // avoids "false" signals
  bool _flag = { false };

  // flag to stop _worker thread
  std::atomic_bool _stop = { false };

  // statistics
  uint32_t _cnt_total{0};
  uint32_t _cnt_block_exe{0};
  uint32_t _cnt_block_que{0};

  // Syncing with CallsQueue (to block proc execution if this one still in queue)

  struct ExeSync {
    uint32_t queued;
    uint32_t done;
  };

  std::map<CallTag, ExeSync> _exe_sync;

  // thread procedure
  void SchedulerProc();

  // methods below are NOT thread-safe, they must be synced at point of call!

  // must be called when the lambda put in CallsQueue for execution
  void OnExeQueued(CallTag id);
  // must be called from within lambda executed by CallsQueue
  void OnExeDone(CallTag id);
  // must be called before put lambda in CallsQueue to avoid duplicated lambdas in CallsQueue
  bool CanExe(CallTag id);
};
