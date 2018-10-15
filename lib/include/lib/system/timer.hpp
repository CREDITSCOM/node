#ifndef TIMER_HPP
#define TIMER_HPP

#include <functional>
#include <memory>
#include <atomic>
#include <thread>
#include <chrono>
#include <vector>

namespace cs
{
    using TimerCallback = std::function<void()>;

    ///
    /// Represents standard timer that calls callbacks every msec with time correction.
    ///
    /// @brief Timer uses callback in another thread.
    ///
    class Timer
    {
    public:
        enum : unsigned int {
            RangeDeltaInPercents = 10
        };

        explicit Timer();
        ~Timer();

        ///
        /// Starts timer with milliseconds period.
        ///
        /// @param msec is time in msec to tick.
        ///
        void start(int msec);

        ///
        /// Stops timer.
        ///
        /// @brief Timer would not stop immediatly, only after thread joining.
        ///
        void stop();

        ///
        /// Registers callback to timer
        ///
        /// @param callback is any functor, lambda, closure, function object.
        /// @brief You can add any count of callbacks to call.
        /// Method should be called only if isRunning returns false.
        ///
        void connect(const TimerCallback& callback);

        ///
        /// Unregisters all callbacks.
        ///
        void disconnect();

        ///
        /// Returns timer status.
        ///
        /// @return Returns timer running state.
        ///
        bool isRunning();

        ///
        /// Calls callback once in another thread after msec time.
        ///
        /// @param msec is time in msec to tick.
        /// @param callback is any functor, lambda, closure, function object.
        ///
        static void singleShot(int msec, const TimerCallback& callback);

    protected:

        // timer main loop
        void loop();

        // timer rehabilitation when timer degradate
        void rehabilitation();

    private:
        bool m_isRunning;
        bool m_isRehabilitation;

        std::atomic<bool> m_interruption;
        std::thread m_thread;
        std::vector<TimerCallback> m_callbacks;

        unsigned int m_allowableDifference;
        std::chrono::milliseconds m_msec;
        std::chrono::milliseconds m_realMsec;
        std::chrono::time_point<std::chrono::system_clock> m_rehabilitationStartValue;
    };
}

#endif // ! TIMER_H
