#ifndef TIMER_HPP
#define TIMER_HPP

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

#include <lib/system/signals.hpp>

namespace cs {
using TimerCallbackSignature = void();
using TimerCallback = std::function<TimerCallbackSignature>;
using TimeOutSignal = cs::Signal<TimerCallbackSignature>;

///
/// Represents standard timer that calls callbacks every msec with time correction.
///
/// @brief Timer uses callback in another thread.
///
class Timer {
public:
  enum : unsigned int {
    RangeDeltaInPercents = 10
  };

  Timer();
  ~Timer();

  ///
  /// @brief Starts timer with milliseconds period.
  /// @param msec is time in msec to tick.
  ///
  void start(int msec);

  ///
  /// @brief Stops timer.
  /// @brief Timer would not stop immediatly, only after thread joining.
  ///
  void stop();

  ///
  /// @brief Returns timer status.
  ///
  bool isRunning();

  ///
  /// @brief Calls callback once in another thread after msec time.
  /// @param msec is time in msec to tick.
  /// @param callback is any functor, lambda, closure, function object.
  ///
  static void singleShot(int msec, const TimerCallback& callback);

public signals:

  /// generates when timer ticks
  TimeOutSignal timeOut;

protected:
  // timer main loop
  void loop();

  // timer rehabilitation when timer degradate
  void rehabilitation();

private:
  bool isRunning_;
  bool isRehabilitation_;

  std::atomic<bool> interruption_;
  std::thread timerThread_;
  std::vector<TimerCallback> callbacks_;

  unsigned int allowDifference_;
  std::chrono::milliseconds ms_;
  std::chrono::milliseconds realMs_;
  std::chrono::time_point<std::chrono::system_clock> rehabilitationStartValue_;
};
}  // namespace cs

#endif  //  TIMER_HPP
