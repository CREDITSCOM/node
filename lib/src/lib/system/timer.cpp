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

void cs::Timer::start(int msec, Type type, RunPolicy policy) {
    interruption_ = false;
    isRunning_ = true;

    type_ = type;
    policy_.store(policy, std::memory_order_release);

    ms_ = std::chrono::milliseconds(msec);
    ns_ = 0;

    realMs_ = ms_;
    allowDifference_ = static_cast<unsigned int>(msec) * RangeDeltaInPercents / 100;

    try {
        timerThread_ = (type_ == Type::Standard) ? std::thread(&Timer::loop, this) : std::thread(&Timer::preciseLoop, this);
    }
    catch (const std::exception& exception) {
        cserror() << "Can not run Timer thread, " << exception.what();
        stop();
    }
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
            start(static_cast<int>(ms_.count()), type_, policy_);
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

void cs::Timer::singleShot(int msec, cs::RunPolicy policy, cs::TimerCallback callback) {
    cs::Concurrent::runAfter(std::chrono::milliseconds(msec), policy, std::move(callback));
}

void cs::Timer::loop() {
    while (!interruption_) {
        if (isRehabilitation_) {
            isRehabilitation_ = false;
            rehabilitationStartValue_ = std::chrono::system_clock::now();
        }

        std::this_thread::sleep_for(ms_);

        rehabilitation();
        call();
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
            call();
        }

        previousTimePoint = now;
        std::this_thread::sleep_for(std::chrono::milliseconds(HighPreciseTimerSleepTimeMs));
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

void cs::Timer::call() {
    auto policy = policy_.load(std::memory_order_acquire);

    if (policy == RunPolicy::ThreadPolicy) {
        emit timeOut();
    }
    else {
        CallsQueue::instance().insert([=] {
            emit timeOut();
        });
    }
}
