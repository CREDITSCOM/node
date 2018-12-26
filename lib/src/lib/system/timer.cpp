#include "lib/system/timer.hpp"
#include <lib/system/concurrent.hpp>

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

void cs::Timer::start(int msec) {
  interruption_ = false;
  isRunning_ = true;
  ms_ = std::chrono::milliseconds(msec);
  timerThread_ = std::thread(&Timer::loop, this);
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

bool cs::Timer::isRunning() {
  return isRunning_;
}

void cs::Timer::singleShot(int msec, const cs::TimerCallback& callback) {
  cs::Concurrent::runAfter(std::chrono::milliseconds(msec), callback);
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

void cs::Timer::rehabilitation() {
  isRehabilitation_ = true;

  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() -
                                                                        rehabilitationStartValue_);
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
