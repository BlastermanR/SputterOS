/**
 * @file NonBlockingStopwatch.cpp
 * @brief Implementation of a non-blocking timing helper.
 *
 * Provides simple millisecond timing without blocking the calling task.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/5/26
 */

#include "sputteros/utils/NonBlockingStopwatch.h"

namespace SputterOS
{

NonBlockingStopwatch::NonBlockingStopwatch() : m_startTime(0), m_running(false)
{
    // Intentionally Empty
}

void NonBlockingStopwatch::start(SputterMicros currentTime)
{
    m_startTime = currentTime;
    m_running   = true;
}

void NonBlockingStopwatch::stop() { m_running = false; }

SputterMicros NonBlockingStopwatch::elapsed(SputterMicros currentTime) const
{
    if (!m_running)
    {
        return 0;
    }
    return currentTime - m_startTime;
}

bool NonBlockingStopwatch::hasExpired(SputterMicros currentTime, SputterMicros duration) const
{
    return m_running && (elapsed(currentTime) >= duration);
}

bool NonBlockingStopwatch::isRunning() const { return m_running; }

void NonBlockingStopwatch::reset()
{
    m_startTime = 0;
    m_running   = false;
}

} // namespace SputterOS
