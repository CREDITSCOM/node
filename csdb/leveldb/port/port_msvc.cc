#include <unistd.h>
#include <windows.h>
#include "port/port_msvc.h"
#include "port/win/sys/time.h"

namespace leveldb {
namespace port {
Mutex::Mutex() :
  m_(new CRITICAL_SECTION)
{
  ::InitializeCriticalSection(static_cast<CRITICAL_SECTION*>(m_));
}

Mutex::~Mutex() {
  ::DeleteCriticalSection(static_cast<CRITICAL_SECTION*>(m_));
  delete static_cast<CRITICAL_SECTION*>(m_);
}

void Mutex::Lock() {
  ::EnterCriticalSection(static_cast<CRITICAL_SECTION*>(m_));
}

void Mutex::Unlock() {
  ::LeaveCriticalSection(static_cast<CRITICAL_SECTION*>(m_));
}

CondVar::CondVar(Mutex* mu) :
  cv_(new CONDITION_VARIABLE), m_(mu)
{
  ::InitializeConditionVariable(static_cast<CONDITION_VARIABLE*>(cv_));
}

CondVar::~CondVar() {
  delete static_cast<CONDITION_VARIABLE*>(cv_);
}

void CondVar::Wait() {
  ::SleepConditionVariableCS(static_cast<CONDITION_VARIABLE*>(cv_), static_cast<CRITICAL_SECTION*>(m_->m_), INFINITE);
}

void CondVar::Signal() {
  ::WakeConditionVariable(static_cast<CONDITION_VARIABLE*>(cv_));
}

void CondVar::SignalAll() {
  ::WakeAllConditionVariable(static_cast<CONDITION_VARIABLE*>(cv_));
}

uint32_t AcceleratedCRC32C(uint32_t /*crc*/, const char* /*buf*/, size_t /*size*/) {
  return 0;
}

}  // namespace port
}  // namespace leveldb

int geteuid() {
  return 0;
}

struct timezone
{
  int  tz_minuteswest; /* minutes W of Greenwich */
  int  tz_dsttime;     /* type of dst correction */
};

int gettimeofday(struct timeval* tv, struct timezone* tz)
{
  static int tzflag = 0;

  if (NULL != tv) {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);

    uint64_t t = (((static_cast<uint64_t>(ft.dwHighDateTime) << 32)
                  | static_cast<uint64_t>(ft.dwLowDateTime))
                 - static_cast<uint64_t>(116444736000000000ULL)) / 10ULL;

    tv->tv_sec = (long)(t / 1000000ULL);
    tv->tv_usec = (long)(t % 1000000ULL);
  }

  if (NULL != tz) {
    if (!tzflag) {
      _tzset();
      ++tzflag;
    }
    long msw;
    _get_timezone(&msw);
    tz->tz_minuteswest = static_cast<int>(msw) / 60;
    _get_daylight(&tz->tz_dsttime);
  }
  return 0;
}

struct tm* localtime_r(const time_t *clock, struct tm *result)
{
  localtime_s(result, clock);
  return result;
}
