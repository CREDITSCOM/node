#ifndef TIMER_HPP
#define TIMER_HPP

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

#include <lib/system/concurrent.hpp>

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

  enum class Type {
    Standard,
    HighPrecise
  };

  Timer();
  ~Timer();

  ///
  /// @brief Starts timer with milliseconds period.
  /// @param msec is time in msec to tick.
  /// @param type is timer type to use.
  ///
  void start(int msec, Type type = Type::Standard);

  ///
  /// @brief Stops timer.
  /// @brief Timer would not stop immediatly, only after thread joining.
  ///
  void stop();

  ///
  /// @brief Returns timer status.
  ///
  bool isRunning() const;

  ///
  /// @brief Returns current timer type.
  ///
  Type type() const;

  ///
  /// @brief Calls callback once in another thread after msec time.
  /// @param msec is time in msec to tick.
  /// @param callback is any functor, lambda, closure, function object.
  ///
  static void singleShot(int msec, cs::RunPolicy policy, const TimerCallback& callback);

public signals:

  /// generates when timer ticks
  TimeOutSignal timeOut;

protected:
  // timer main loop
  void loop();
  void preciseLoop();

  // timer rehabilitation when timer degradate
  void rehabilitation();

private:
  bool isRunning_;
  bool isRehabilitation_;
  std::atomic<bool> interruption_;

  std::thread timerThread_;
  Type type_;

  unsigned int allowDifference_;
  std::chrono::milliseconds ms_;
  std::chrono::nanoseconds ns_;

  std::chrono::milliseconds realMs_;
  std::chrono::time_point<std::chrono::system_clock> rehabilitationStartValue_;
};
}  // namespace cs

#endif  //  TIMER_HPP
