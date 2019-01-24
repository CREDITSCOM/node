#include "lib/system/timer.hpp"

cs::Timer::Timer()
: isRunning_(false)
, isRehabilitation_(true)
, interruption_(false)
, ms_(std::chrono::milliseconds(0)) {
}

cs::Timer::~Timer() {
  if (isRunning()) {
    stop();
  }
}

void cs::Timer::start(int msec, Type type) {
  interruption_ = false;
  isRunning_ = true;

  type_ = type;

  ms_ = std::chrono::milliseconds(msec);
  ns_ = 0;

  timerThread_ = (type_ == Type::Standard) ? std::thread(&Timer::loop, this) : std::thread(&Timer::preciseLoop, this);

  realMs_ = ms_;
  allowDifference_ = static_cast<unsigned int>(msec) * RangeDeltaInPercents / 100;
}

void cs::Timer::stop() {
  interruption_ = true;

  if (timerThread_.joinable()) {
    timerThread_.join();
    isRunning_ = false;
  }
}

void cs::Timer::restart() {
  if (isRunning_) {
    if (type_ == Type::Standard) {
      stop();
      start(static_cast<int>(ms_.count()));
    }
    else {
      ns_ = 0;
    }
  }
}

bool cs::Timer::isRunning() const {
  return isRunning_;
}

cs::Timer::Type cs::Timer::type() const {
  return type_;
}

void cs::Timer::singleShot(int msec, cs::RunPolicy policy, const cs::TimerCallback& callback) {
  cs::Concurrent::runAfter(std::chrono::milliseconds(msec), policy, callback);
}

void cs::Timer::loop() {
  while (!interruption_) {
    if (isRehabilitation_) {
      isRehabilitation_ = false;
      rehabilitationStartValue_ = std::chrono::system_clock::now();
    }

    std::this_thread::sleep_for(ms_);

    rehabilitation();

    emit timeOut();
  }
}

void cs::Timer::preciseLoop() {
  std::chrono::high_resolution_clock::time_point previousTimePoint = std::chrono::high_resolution_clock::now();

  while (!interruption_) {
    auto now = std::chrono::high_resolution_clock::now();
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now - previousTimePoint);
    ns_ += ns.count();

    auto needMsInNs = std::chrono::duration_cast<std::chrono::nanoseconds>(ms_);

    if (needMsInNs.count() <= ns_) {
      ns_ = 0;

      emit timeOut();
    }

    // recalc
    previousTimePoint = now;
    std::this_thread::yield();
  }
}

void cs::Timer::rehabilitation() {
  isRehabilitation_ = true;

  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - rehabilitationStartValue_);
  auto difference = duration - realMs_;

  if (difference >= realMs_) {
    ms_ = std::chrono::milliseconds(0);
  }
  else {
    if (difference.count() > allowDifference_) {
      ms_ = realMs_ - (difference % realMs_);
    }
    else {
      if (ms_ != realMs_) {
        ms_ = realMs_;
      }
    }
  }
}
