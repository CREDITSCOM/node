#pragma once

#include <condition_variable>
#include <mutex>
#include <thread>

#include <pthread.h>
#include <signal.h>

#include "service_owner.hpp"

namespace cs {

class Service {
public:
    Service(ServiceOwner&, const char* serviceName = nullptr);

    bool run();

private:
    using mutex_type = std::mutex;
    using lock_type = std::unique_lock<mutex_type>;
    using cond_var_type = std::condition_variable;
    using thread_type = std::thread;

    struct ThreadsStatus {
        bool signalReady = false;
        bool workerReady = false;
        bool mainReady = false;
        bool error = false;
    };

    void waitForSignals();
    void doWork();
    bool startSignalThread();
    bool startWorkerThread();

    ServiceOwner& owner_;
    const char* serviceName_;

    thread_type signalThread_;
    thread_type workerThread_;

    mutex_type mux_;
    cond_var_type cv_;

    ThreadsStatus threadsStatus_;
};

inline Service::Service(ServiceOwner& owner, const char* serviceName)
    : owner_(owner), serviceName_(serviceName) {}

inline bool Service::run() {
    auto pid = ::fork();
#ifndef DISABLE_DAEMON
    switch (pid) {
        // child
        case 0 :
            if (!owner_.onFork(serviceName_, pid)) return false;
            break;
        // exit from parent or error
        default :
            return owner_.onFork(serviceName_, pid);
    }
#endif // !DISABLE_DAEMON
    sigset_t signals;
    sigfillset(&signals);
    if (pthread_sigmask(SIG_BLOCK, &signals, nullptr) != 0) {
        return false;
    }

    bool result = true;

    try {
        if (!startSignalThread() || !startWorkerThread()) {
            result = false;
            lock_type lock(mux_);
            threadsStatus_.mainReady = true;
            cv_.notify_all();
        }
        else {
            
        }
    }
    catch (...) {
        result &= owner_.onException();
    }

    if (signalThread_.joinable()) {
        signalThread_.join();
    }
    if (workerThread_.joinable()) {
        workerThread_.join();
    }

    return result;
}

inline void Service::waitForSignals() {

}

inline void Service::doWork() {

}

inline bool Service::startSignalThread() {
    signalThread_ = thread_type(&Service::waitForSignals, this);

    lock_type lock(mux_);
    cv_.wait(lock, [this]() { return threadsStatus_.signalReady; });
    threadsStatus_.signalReady = false;
    bool error = threadsStatus_.error;
    threadsStatus_.error = false;

    return !error;
}

inline bool Service::startWorkerThread() {
    workerThread_ = thread_type(&Service::doWork, this);

    lock_type lock(mux_);
    cv_.wait(lock, [this]() { return threadsStatus_.workerReady; });
    threadsStatus_.workerReady = false;
    bool error = threadsStatus_.error;
    threadsStatus_.error = false;

    return !error;
}

} // namespace cs
