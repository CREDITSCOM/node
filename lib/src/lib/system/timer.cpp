#include "lib/system/timer.hpp"
#include <lib/system/structures.hpp>

cs::Timer::Timer():
    m_isRunning(false),
    m_isRehabilitation(true),
    m_interruption(false),
    m_msec(std::chrono::milliseconds(0))
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
    m_interruption = false;
    m_isRunning = true;
    m_msec = std::chrono::milliseconds(msec);
    m_thread = std::thread(&Timer::loop, this);
    m_realMsec = m_msec;
    m_allowableDifference = static_cast<unsigned int>(msec) * RangeDeltaInPercents / 100;
}

void cs::Timer::stop()
{
    m_interruption = true;

    if (m_thread.joinable())
    {
        m_thread.join();
        m_isRunning = false;
    }
}

void cs::Timer::connect(const TimerCallback& callback)
{
    m_callbacks.push_back(callback);
}

void cs::Timer::disconnect()
{
    m_callbacks.clear();
}

bool cs::Timer::isRunning()
{
    return m_isRunning;
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
    while (!m_interruption)
    {
        if (m_isRehabilitation)
        {
            m_isRehabilitation = false;
            m_rehabilitationStartValue = std::chrono::system_clock::now();
        }

        std::this_thread::sleep_for(m_msec);

        rehabilitation();

        for (const auto& callback : m_callbacks) {
            callback();
        }
    }
}

void cs::Timer::rehabilitation()
{
    m_isRehabilitation = true;

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - m_rehabilitationStartValue);
    auto difference = duration - m_realMsec;

    if (difference >= m_realMsec) {
        m_msec = std::chrono::milliseconds(0);
    }
    else
    {
        if (difference.count() > m_allowableDifference) {
            m_msec = m_realMsec - (difference % m_realMsec);
        }
        else
        {
            if (m_msec != m_realMsec) {
                m_msec = m_realMsec;
            }
        }
    }
}
