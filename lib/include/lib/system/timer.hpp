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

class Timer;
using TimerPtr = std::shared_ptr<Timer>;

///
/// Represents standard timer that calls callbacks every msec with time correction.
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

  void start(int msec, Type type = Type::Standard);
  void stop();
  void restart();

  bool isRunning() const;
  Type type() const;

  static void singleShot(int msec, cs::RunPolicy policy, TimerCallback callback);
  static TimerPtr create();

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
  std::atomic<int64_t> ns_;

  std::chrono::milliseconds realMs_;
  std::chrono::time_point<std::chrono::system_clock> rehabilitationStartValue_;
};
}  // namespace cs

#endif  //  TIMER_HPP
