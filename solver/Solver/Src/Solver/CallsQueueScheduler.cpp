#include "CallsQueueScheduler.h"
#include <lib/system/structures.hpp> // CallsQueue

void CallsQueueScheduler::Run()
{
    _stop = false;
    _worker = std::thread([this]() {
        bool signaled { false };
        constexpr long long min_wait_for = 5;
        while(!_stop)
        {
            // get earliest action time
            auto earliest = ClockType::now() + std::chrono::seconds(60);
            {
                std::lock_guard<std::mutex> lque(_mtx_queue);
                if(!_queue.empty()) {
                    earliest = _queue.cbegin()->tp;
                }
            }
            // sleep until scheduled event and get ready to awake at any time
            std::unique_lock<std::mutex> lsig(_mtx_signal);
            signaled = _signal.wait_until(lsig, earliest, [this]() { return _flag; }); // std::system_error!
            // test stop condition before reaction
            if(_stop) {
                break;
            }
            // event occurs
            if(signaled) {
                // reset _flag for the next signal
                _flag = false;
                // awake by direct notification: re-schedule next timeout
                std::lock_guard<std::mutex> lque(_mtx_queue);
                if(!_queue.empty()) {
                    // проверить, не пора ли выполнять сразу
                    if((_queue.cbegin()->tp - ClockType::now()).count() >= min_wait_for) {
                        // schedule next wait period
                        continue;
                    }
                    // fall through to execute proc now (either timeout almost coincided with the signal, or proc is scheduled with tiny wait_for)
                }
                else {
                    // queue is empty, schedule next wait period
                    continue;
                }
            }
            // awake due timeout: the most probable its time to execute earliest proc
            // test scheduled time (if it was at all)
            Context run;
            run.id = no_id;
            {
                std::lock_guard<std::mutex> lque(_mtx_queue);
                if(!_queue.empty()) {
                    if((_queue.cbegin()->tp - ClockType::now()).count() < min_wait_for) {
                        run = *_queue.cbegin();
                        _queue.erase(_queue.cbegin());
                        if(run.dt > 0) {
                            // LaunchScheme::periodic -> schedule next item
                            if(no_id == Insert(std::chrono::milliseconds(run.dt), run.proc, LaunchScheme::periodic, run.comment)) {
                                // periodic calls aborted due to unexpected problem!
                            }
                        }
                    }
                }
            }
            if(run.id != no_id) {
                ProcType proc = _queue.cbegin()->proc;
                CallsQueue::instance().insert([proc]() {
                    proc();
                });
                _count_total.fetch_add(1);
            }
        }
    });
}

void CallsQueueScheduler::Stop()
{
    _stop = true;
    _flag = true;
    // awake worker thread if it sleeps
    _signal.notify_one();
    if(_worker.joinable()) {
        _worker.join();
    }
}

uintptr_t CallsQueueScheduler::Insert(ClockType::duration wait_for, ProcType proc, LaunchScheme scheme, const std::string& comment)
{
    uintptr_t id = (uintptr_t) &proc;
    {
        std::lock_guard<std::mutex> l(_mtx_queue);
        if(std::find(_queue.cbegin(), _queue.cend(), id) != _queue.cend()) {
            // block already added before
            return no_id;
        }
        // add new item
        auto result = _queue.insert(CallsQueueScheduler::Context {
            id,
            ClockType::now() + wait_for,
            ( scheme == LaunchScheme::single ? 0 : std::chrono::duration_cast<std::chrono::milliseconds>(wait_for).count() ),
            std::move(proc),
            std::move(comment)
        });
        if(!result.second) {
            return no_id;
        }
    }
    // awake worker thread to re-schedule its waiting
    _signal.notify_one();
    return id;
}

bool CallsQueueScheduler::Remove(uintptr_t id)
{
    {
        std::lock_guard<std::mutex> l(_mtx_queue);
        auto it = std::find(_queue.cbegin(), _queue.cend(), id);
        if(it == _queue.cend()) {
            return false;
        }
        _queue.erase(it);
    }
    // awake worker thread to re-schedule its waiting
    _signal.notify_one();
    return true;
}

uint32_t CallsQueueScheduler::TotalExecutedCalls() const
{
    return _count_total;
}
