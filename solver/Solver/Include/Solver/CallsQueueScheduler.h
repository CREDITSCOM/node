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
 
    enum class LaunchScheme {

        ///< An enum constant representing the single option: launch once, cancel if previous schedule call still is not done
        single,

        ///< An enum constant representing the periodic single option: launch periodically, schedule call cancels if already running one cycle
        periodic
    };

    using ClockType = std::chrono::steady_clock;
    using ProcType = std::function<void()>;

    constexpr static uintptr_t no_id = 0;

    CallsQueueScheduler()
        :_queue( compare )
    {
    }

    CallsQueueScheduler(const CallsQueueScheduler&) = delete;
    CallsQueueScheduler& operator =(const CallsQueueScheduler&) = delete;

    void Run();

    void Stop();

    uintptr_t Insert(ClockType::duration wait_for, ProcType proc, LaunchScheme scheme, const std::string& comment);

    bool Remove(uintptr_t id);

    uint32_t TotalExecutedCalls() const;

private:

    struct Context
    {
        uintptr_t id;
        ClockType::time_point tp;
        //LaunchScheme scheme;
        long long dt;
        ProcType proc;
        std::string comment;

        bool operator==(uintptr_t rhs) const
        {
            return id == rhs;
        }
    };

    std::function<bool(const Context& lhs, const Context& rhs)> compare =
        [](const Context& lhs, const Context& rhs) { return lhs.tp < rhs.tp; };
    //std::priority_queue<Context, std::vector<Context>, decltype(compare)> _queue;
    std::set<Context, decltype(compare)> _queue;
    std::mutex _mtx_queue;

    std::condition_variable _signal;
    std::thread _worker;
    std::mutex _mtx_signal;

    bool _flag { false };
    std::atomic_bool _stop { false };

    // statistics
    std::atomic_uint32_t _count_total { 0 };
};
