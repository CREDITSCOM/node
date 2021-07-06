#pragma once

#include <condition_variable>
#include <mutex>
#include <thread>

#include <fcntl.h>
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
        int signalCode = 0;
    };

    void waitForSignals();
    void doWork();
    bool startSignalThread();
    bool startWorkerThread();
    void joinThreads();
    bool startMonitoring();

    void closeIO();

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
#ifndef DISABLE_DAEMON
    auto pid = ::fork();
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
        }
        else {
#ifndef DISABLE_DAEMON
            setsid();
            chdir("/");
            closeIO();
#endif // !DISABLE_DAEMON
            {
                lock_type lock(mux_);
                threadsStatus_.mainReady = true;
                cv_.notify_all();
            }
            result = startMonitoring();
        }
    }
    catch (...) {
        joinThreads();
        result &= owner_.onException();
    }

    joinThreads();
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

inline void Service::joinThreads() {
    if (signalThread_.joinable()) {
        signalThread_.join();
    }
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
}

inline void Service::closeIO() {
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    close(STDIN_FILENO);
    int devNull = open("/dev/null", O_RDWR);
    if (devNull != -1) {
        dup2(devNull, STDOUT_FILENO);
        dup2(devNull, STDERR_FILENO);
        dup2(devNull, STDIN_FILENO);
        close(devNull);
    }
}

inline bool Service::startMonitoring() {
    bool result = true;
    bool paused = false;
    while (true) {
        ThreadsStatus threadStatus;
        {
            lock_type lock(mux_);
            cv_.wait(
                lock,
                [this]() {
                    return threadsStatus_.signalReady || threadsStatus_.workerReady;
                }
            );
            threadStatus = threadsStatus_;
            threadsStatus_ = ThreadsStatus();
        }
        if (threadStatus.workerReady) {
            result = !threadStatus.error;
            pthread_kill(signalThread_.native_handle(), SIGTERM);
            break;
        }
        else if (threadStatus.signalReady) {
            switch (threadStatus.signalCode) {
                case SIGTERM :
                    owner_.onStop();
                    break;
                case SIGHUP :
                    owner_.onParamChange();
                    break;
                case SIGINT :
                    if (paused) {
                        owner_.onContinue();
                    }
                    else {
                        owner_.onPause();
                    }
                    paused = !paused;
            }
        }
    }
    return result;
}

} // namespace cs
