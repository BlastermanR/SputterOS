/**
 * @file MemoryProfiler.cpp
 * @brief Implementation of lightweight heap and stack profiling helpers.
 *
 * Tracks free heap changes and peak memory usage for diagnostics.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/5/26
 */

#include "sputteros/utils/MemoryProfiler.h"

namespace SputterOS
{

MemoryProfiler::MemoryProfiler() : m_baselineFreeBytes(0), m_peakUsedBytes(0)
{
    m_baselineFreeBytes = getFreeHeapBytes();
}

void MemoryProfiler::update()
{
    const std::size_t freeNow = getFreeHeapBytes();
    const std::size_t usedNow = (freeNow <= m_baselineFreeBytes) ? (m_baselineFreeBytes - freeNow) : 0;
    if (usedNow > m_peakUsedBytes)
    {
        m_peakUsedBytes = usedNow;
    }
}

std::size_t MemoryProfiler::getFreeHeapBytes()
{
    // Platform-specific heap query must be provided by the user's HAL layer.
    // Return 0 in the portable library core.
    return 0;
}

std::size_t MemoryProfiler::getStackHighWaterMark()
{
    // Stack high-water marking requires platform support (e.g. FreeRTOS
    // uxTaskGetStackHighWaterMark or Pico SDK stack canary checks).
    // Return 0 in the portable library core.
    return 0;
}

std::size_t MemoryProfiler::getPeakHeapUsedBytes() const { return m_peakUsedBytes; }

void MemoryProfiler::reset()
{
    m_baselineFreeBytes = getFreeHeapBytes();
    m_peakUsedBytes     = 0;
}

} // namespace SputterOS
