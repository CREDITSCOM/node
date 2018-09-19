#include "Solver/CallsQueueScheduler.h"
#include <lib/system/structures.hpp> // CallsQueue
#include <algorithm>

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
                    // test whether to call immediately
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
            run.id = no_tag;
            {
                std::lock_guard<std::mutex> lque(_mtx_queue);
                // execute calls until wait time >= min_wait_for
                while(!_queue.empty()) {
                    if((_queue.cbegin()->tp - ClockType::now()).count() >= min_wait_for) {
                        break;
                    }
                    run = *_queue.cbegin();
                    _queue.erase(_queue.cbegin());
                    ProcType proc = run.proc;
                    // push to CallsQueue only if there are no any previous calls
                    if(CanExe(run.id)) {
                        OnExeQueued(run.id);
                        CallsQueue::instance().insert([this, run]() {
                            run.proc();
                            std::lock_guard<std::mutex> lque(_mtx_queue);
                            OnExeDone(run.id);
                        });
                        _cnt_total += 1;
                    }
                    else {
                        _cnt_block_exe += 1;
                    }
                    // Launch::periodic -> schedule next item
                    if(run.dt > 0) {
                        run.tp = ClockType::now() + std::chrono::milliseconds(run.dt);
                        auto pos = _queue.insert(run);
                        if(pos == _queue.end()) {
                            // periodic calls aborted due to unexpected problem!
                        }
                    }
                }
            }
        }
    });
}

void CallsQueueScheduler::OnExeQueued(CallTag id)
{
    auto it = _exe_sync.find(id);
    if(it == _exe_sync.cend()) {
        _exe_sync.insert({ id, { 1, 0 } });
    }
    else {
        it->second.queued += 1;
    }
}

void CallsQueueScheduler::OnExeDone(CallTag id)
{
    auto it = _exe_sync.find(id);
    if(it != _exe_sync.end()) {
        it->second.done += 1;
    }
}

bool CallsQueueScheduler::CanExe(CallTag id)
{
    auto it = _exe_sync.find(id);
    if(it != _exe_sync.end()) {
        return it->second.done == it->second.queued;
    }
    return true;
}

void CallsQueueScheduler::Stop()
{
    Clear();
    _stop = true;
    // awake worker thread if it sleeps
    _flag = true;
    _signal.notify_one();
    if(_worker.joinable()) {
        _worker.join();
    }
}

CallsQueueScheduler::CallTag CallsQueueScheduler::Insert(ClockType::duration wait_for, const ProcType& proc, Launch scheme, bool replace_existing /*= false*/)
{
    if(!_worker.joinable()) {
        Run();
    }
    //TODO: find better way to identify procs (especially, in case of "in-place" lambdas when those may have the same address)
    // CallTag id = (CallTag) &proc;
    // current solution requires enable RTTI = Yes (/GR) to compile:
    const CallTag id = proc.target_type().hash_code();
    {
        std::lock_guard<std::mutex> l(_mtx_queue);
        auto it = std::find(_queue.cbegin(), _queue.cend(), id);
        if( it != _queue.cend()) {
            if(!replace_existing) {
                // reject schedule, the one already added before and still in queue
                _cnt_block_que += 1;
                return id;
            }
            else {
                // remove from queue, below we will add a new schedule
                _queue.erase(it);
            }
        }
        // add new item
        auto result = _queue.insert(CallsQueueScheduler::Context {
            id,
            ClockType::now() + wait_for,
            ( scheme == Launch::once ? 0 : std::chrono::duration_cast<std::chrono::milliseconds>(wait_for).count() ),
            std::move(proc)
            //, std::move(comment)
        });
        if(result == _queue.end()) {
            return no_tag;
        }
    }
    // awake worker thread to re-schedule its waiting
    _flag = true;
    _signal.notify_one();
    return id;
}

bool CallsQueueScheduler::Remove(CallsQueueScheduler::CallTag id)
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
    _flag = true;
    _signal.notify_one();
    return true;
}

void CallsQueueScheduler::RemoveAll()
{
    {
        std::lock_guard<std::mutex> l(_mtx_queue);
        _queue.clear();
    }
    // awake worker thread to re-schedule its waiting
    _flag = true;
    _signal.notify_one();
}

void CallsQueueScheduler::Clear()
{
    {
        std::lock_guard<std::mutex> l(_mtx_queue);
        _queue.clear();
        _exe_sync.clear();
    }
    // awake worker thread to re-schedule its waiting
    _flag = true;
    _signal.notify_one();
}
