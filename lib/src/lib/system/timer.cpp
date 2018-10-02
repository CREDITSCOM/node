#include "lib/system/timer.hpp"
#include <lib/system/structures.hpp>

cs::Timer::Timer():
    mIsRunning(false),
    mInterruption(false),
    mIsRehabilitation(true),
    mMsec(std::chrono::milliseconds(0))
{
}

cs::Timer::~Timer()
{
    if (isRunning()) {
        stop();
    }
}

void cs::Timer::start(int msec)
{
    mInterruption = false;
    mIsRunning = true;
    mMsec = std::chrono::milliseconds(msec);
    mThread = std::thread(&Timer::loop, this);
    mRealMsec = mMsec;
    mAllowableDifference = RangeDeltaInPercents ? (static_cast<unsigned int>(msec) * RangeDeltaInPercents / 100) : 0;
}

void cs::Timer::stop()
{
    mInterruption = true;

    if (mThread.joinable())
    {
        mThread.join();
        mIsRunning = false;
    }
}

void cs::Timer::connect(const TimerCallback& callback)
{
    mCallbacks.push_back(callback);
}

void cs::Timer::disconnect()
{
    mCallbacks.clear();
}

bool cs::Timer::isRunning()
{
    return mIsRunning;
}

static void singleShotHelper(int msec, const cs::TimerCallback& callback)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(msec));

    CallsQueue::instance().insert(callback);
}

void cs::Timer::singleShot(int msec, const cs::TimerCallback& callback)
{
    std::thread thread = std::thread(&singleShotHelper, msec, callback);
    thread.detach();
}

void cs::Timer::loop()
{
    while (!mInterruption)
    {
        if (mIsRehabilitation)
        {
            mIsRehabilitation = false;
            mRehabilitationStartValue = std::chrono::system_clock::now();
        }

        std::this_thread::sleep_for(mMsec);

        rehabilitation();

        for (const auto& callback : mCallbacks) {
            callback();
        }
    }
}

void cs::Timer::rehabilitation()
{
    mIsRehabilitation = true;

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - mRehabilitationStartValue);
    auto difference = duration - mRealMsec;

    if (difference >= mRealMsec) {
        mMsec = std::chrono::milliseconds(0);
    }
    else
    {
        if (difference.count() > mAllowableDifference) {
            mMsec = mRealMsec - (difference % mRealMsec);
        }
        else
        {
            if (mMsec != mRealMsec) {
                mMsec = mRealMsec;
            }
        }
    }
}
