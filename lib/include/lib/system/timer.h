#ifndef TIMER_H
#define TIMER_H

#include <functional>
#include <memory>
#include <atomic>
#include <thread>
#include <chrono>
#include <vector>

namespace cs
{
    using TimerCallback = std::function<void()>;

    // Represents standard timer that calls callbacks every msec with time correction
    class Timer
    {
    public:
        explicit Timer();
        ~Timer();

        // starts timer with milliseconds period
        void start(int msec);

        // stops timer
        void stop();

        // register callback
        void connect(const TimerCallback& callback);

        // unregister callbacks
        void disconnect();

        // returns timer status
        bool isRunning();

        // calls callback after msec time
        static void singleShot(int msec, const TimerCallback& callback);

    protected:

        // timer main loop
        void loop();

        // timer rehabilitation when timer degradate
        void rehabilitation();

    private:

        bool mIsRunning;
        std::atomic<bool> mInterruption;
        bool mIsRehabilitation;
        std::thread mThread;
        std::chrono::milliseconds  mMsec;
        std::vector<TimerCallback> mCallbacks;
        unsigned int mAllowableDifference;
        std::chrono::milliseconds  mRealMsec;
        std::chrono::time_point<std::chrono::system_clock> mRehabilitationStartValue;
    };
}

#endif // ! TIMER_H