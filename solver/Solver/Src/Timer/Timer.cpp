#include "Timer/Timer.h"

const unsigned int RANGE_ALLOWABLE_ERROR_IN_PERCENT = 10;

Credits::CTimer::CTimer():
  mIsRunning(false),
  mInterruption(false),
  mIsRehabilitation(true),
  mMsec(std::chrono::milliseconds(0))
{
  // Empty
}

Credits::CTimer::~CTimer()
{
  stop();
}

void Credits::CTimer::start(int msec)
{
  mInterruption        = false;
  mIsRunning           = true;
  mMsec                = std::chrono::milliseconds(msec);
  mThread              = std::thread(&CTimer::loop, this);
  mRealMsec            = mMsec;
  mAllowableDifference = RANGE_ALLOWABLE_ERROR_IN_PERCENT ?
                         msec * RANGE_ALLOWABLE_ERROR_IN_PERCENT / 100 :
                         0;
}

void Credits::CTimer::stop()
{
  mInterruption = true;

  if (mThread.joinable())
  {
    mThread.join();
    mIsRunning = false;
  }
}

void Credits::CTimer::connect(const TimerCallback& callback)
{
  mCallbacks.push_back(callback);
}

void Credits::CTimer::disconnect()
{
  mCallbacks.clear();
}

bool Credits::CTimer::isRunning()
{
  return mIsRunning;
}

static void singleShotHelper(int msec, const Credits::TimerCallback& callback)
{
  std::this_thread::sleep_for(std::chrono::milliseconds(msec));

  if (callback)
    callback();
}

void Credits::CTimer::singleShot(int msec, const Credits::TimerCallback& callback)
{
  std::unique_ptr<std::thread> thread = std::make_unique<std::thread>(&singleShotHelper, msec, callback);
  thread->detach();
}

void Credits::CTimer::loop()
{
  while (!mInterruption)
  {
    if (mIsRehabilitation)
    {
      mIsRehabilitation         = false;
      mRehabilitationStartValue = std::chrono::system_clock::now();
    }

    std::this_thread::sleep_for(mMsec);

    rehabilitation();

    for (const auto& callback : mCallbacks)
      callback();
  }
}

void Credits::CTimer::rehabilitation()
{
  mIsRehabilitation = true;
  auto duration     = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - mRehabilitationStartValue);
  auto difference   = duration - mRealMsec;

  if (difference >= mRealMsec)
    mMsec = std::chrono::milliseconds(0);
  else
  if (difference.count() > mAllowableDifference)
    mMsec = mRealMsec - (difference % mRealMsec);
  else
  if (mMsec != mRealMsec)
    mMsec = mRealMsec;
}