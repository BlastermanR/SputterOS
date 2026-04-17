/**
 * @file ErrorLogger.cpp
 * @brief Implementation of the ring-buffer error logger.
 *
 * Provides a lightweight circular log buffer for diagnostic and sensor
 * error events.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/5/26
 */

#include "sputteros/utils/logging/ErrorLogger.h"

namespace SputterOS
{

ErrorLogger::ErrorLogger() : m_head(0), m_tail(0)
{
    // Intentionally Empty
}

void ErrorLogger::log(ErrorCode code, SputterMicros timestamp, float value)
{
    const std::size_t head = m_head.load(std::memory_order_relaxed);
    const std::size_t next = (head + 1) % kSlots;

    // If the buffer is full, advance tail to discard the oldest entry.
    // Uses compare_exchange so a concurrent read() that already advanced
    // tail does not cause a double-advance.
    if (next == m_tail.load(std::memory_order_acquire))
    {
        std::size_t expected = m_tail.load(std::memory_order_relaxed);
        std::size_t desired  = (expected + 1) % kSlots;
        m_tail.compare_exchange_strong(expected, desired, std::memory_order_release, std::memory_order_relaxed);
    }

    m_buf[head] = {code, timestamp, value};
    m_head.store(next, std::memory_order_release);
}

bool ErrorLogger::read(Entry &entry)
{
    const std::size_t tail = m_tail.load(std::memory_order_relaxed);

    if (tail == m_head.load(std::memory_order_acquire))
    {
        return false;
    }

    entry = m_buf[tail];
    m_tail.store((tail + 1) % kSlots, std::memory_order_release);
    return true;
}

std::size_t ErrorLogger::count() const
{
    const std::size_t head = m_head.load(std::memory_order_acquire);
    const std::size_t tail = m_tail.load(std::memory_order_acquire);
    return (head >= tail) ? (head - tail) : (kSlots - tail + head);
}

void ErrorLogger::clear() { m_tail.store(m_head.load(std::memory_order_relaxed), std::memory_order_release); }

} // namespace SputterOS
