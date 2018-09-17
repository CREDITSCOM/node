#pragma once

#include <cstdint>
#include <chrono>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <set>

//template<typename TResol = std::chrono::milliseconds>
class CallsQueueScheduler
{
public:
 
    enum class Launch {

        ///< An enum constant representing the once option: launch once, cancel if previous schedule call still is not done
        once,

        ///< An enum constant representing the periodic option: launch periodically, schedule call cancels if already running one cycle
        periodic
    };

    using ClockType = std::chrono::steady_clock;
    using ProcType = std::function<void()>;

    constexpr static uintptr_t no_id = 0;

    /**
     * @fn  CallsQueueScheduler::CallsQueueScheduler()
     *
     * @brief   Default constructor
     *
     * @author  aae
     * @date    17.09.2018
     */

    CallsQueueScheduler()
        :_queue( compare )
    {
    }

    CallsQueueScheduler(const CallsQueueScheduler&) = delete;
    CallsQueueScheduler& operator =(const CallsQueueScheduler&) = delete;

    /**
     * @fn  void CallsQueueScheduler::Run();
     *
     * @brief   Runs scheduler in separate thread, is optional for call. Would be automatically called by the first Insert() operation
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
     * @fn  uintptr_t CallsQueueScheduler::Insert(ClockType::duration wait_for, const ProcType& proc, Launch scheme);
     *
     * @brief   Inserts new call into queue according to wait_for parameter. Do check before insert to avoid queuing of duplicated calls
     *
     * @author  aae
     * @date    17.09.2018
     *
     * @param   wait_for    Time to wait before do call a procedure.
     * @param   proc        The procedure to be scheduled for call.
     * @param   scheme      The scheme: once - do one call, periodic - repeat calls every wait_for period.
     *
     * @return  An id that can be used in future to remove scheduled call from queue if any.
     */

    uintptr_t Insert(ClockType::duration wait_for, const ProcType& proc, Launch scheme/*, const std::string& comment*/);

    /**
     * @fn  bool CallsQueueScheduler::Remove(uintptr_t id);
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

    bool Remove(uintptr_t id);

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

    uint32_t TotalExecutedCalls() const;

private:

    /**
     * @struct  Context
     *
     * @brief   Stores all info to call, re-schedule and cancel further calls in queue.
     *
     * @author  aae
     * @date    17.09.2018
     */

    struct Context
    {

        /** @brief   The identifier: lets find item in queue (e.g. for remove) */
        uintptr_t id;

        /** @brief   The time point for scheduled execution */
        ClockType::time_point tp;

        /** @brief   The delta - time period for periodic calls */
        long long dt;

        /** @brief   The procedure to call*/
        ProcType proc;
        
        // not used, useful for debug purpose
        //std::string comment;

        // support for std::find() by id
        bool operator==(uintptr_t rhs) const
        {
            return id == rhs;
        }
    };

    // used for sort items in _queue by time to call
    std::function<bool(const Context& lhs, const Context& rhs)> compare =
        [](const Context& lhs, const Context& rhs) { return lhs.tp < rhs.tp; };

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
    bool _flag { false };

    // flag to stop _worker thread
    std::atomic_bool _stop { false };

    // statistics
    std::atomic_uint32_t _count_total { 0 };
};
