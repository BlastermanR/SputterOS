#ifndef SPUTTEROS_KERNEL_SCHEDULERHEALTHMETRICS_H
#define SPUTTEROS_KERNEL_SCHEDULERHEALTHMETRICS_H

/**
 * @file SchedulerHealthMetrics.h
 * @brief Aggregate scheduler health statistics: gap time, overruns, deadline misses.
 *
 * Tracks cumulative and peak gap time (idle time between task dispatches),
 * total overrun and deadline miss counts aggregated across all monitored
 * tasks, and total tick count. Gap time is recorded by `System::tick()`
 * as wall-clock time minus total busy time each tick.
 *
 * @note Zero-heap, header-only. No dynamic allocation.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/11/2026
 */

#include "sputteros/osal/SputterTime.h"
#include <cstdint>

namespace SputterOS
{
namespace Kernel
{

class SchedulerHealthMetrics
{
  public:
    /**
     * @brief Record one tick's gap time.
     * @param gapUs: Idle time in this tick (wall time - busy time) in µs.
     */
    void recordGap(SputterMicros gapUs)
    {
        m_totalGapUs += gapUs;
        if (gapUs > m_maxGapUs)
        {
            m_maxGapUs = gapUs;
        }
        ++m_tickCount;
    }

    /**
     * @brief Set aggregated overrun and deadline miss totals.
     *
     * Called periodically by diagnostics to snapshot the current state
     * from all monitored task timers.
     *
     * @param overruns: Total overrun count across all tasks.
     * @param misses:   Total deadline miss count across all tasks.
     */
    void setAggregates(uint32_t overruns, uint32_t misses)
    {
        m_totalOverruns       = overruns;
        m_totalDeadlineMisses = misses;
    }

    /**
     * @brief Total gap (idle) time accumulated across all ticks.
     */
    SputterMicros totalGapUs() const { return m_totalGapUs; }

    /**
     * @brief Peak gap time observed in a single tick.
     */
    SputterMicros maxGapUs() const { return m_maxGapUs; }

    /**
     * @brief Average gap time per tick.
     * @return Average gap in microseconds, or 0 if no ticks recorded.
     */
    float averageGapUs() const
    {
        if (m_tickCount == 0)
        {
            return 0.0f;
        }
        return static_cast<float>(m_totalGapUs) / static_cast<float>(m_tickCount);
    }

    /**
     * @brief Total overrun count aggregated from all monitored tasks.
     */
    uint32_t totalOverruns() const { return m_totalOverruns; }

    /**
     * @brief Total deadline miss count aggregated from all monitored tasks.
     */
    uint32_t totalDeadlineMisses() const { return m_totalDeadlineMisses; }

    /**
     * @brief Total number of ticks recorded.
     */
    uint32_t tickCount() const { return m_tickCount; }

    /**
     * @brief Reset all metrics.
     */
    void reset()
    {
        m_totalGapUs          = 0;
        m_maxGapUs            = 0;
        m_tickCount           = 0;
        m_totalOverruns       = 0;
        m_totalDeadlineMisses = 0;
    }

  private:
    SputterMicros m_totalGapUs{0};          /**< @brief Cumulative gap time (µs). */
    SputterMicros m_maxGapUs{0};            /**< @brief Peak single-tick gap (µs). */
    uint32_t      m_tickCount{0};           /**< @brief Total ticks tracked. */
    uint32_t      m_totalOverruns{0};       /**< @brief Aggregate overruns from all tasks. */
    uint32_t      m_totalDeadlineMisses{0}; /**< @brief Aggregate deadline misses from all tasks. */
};

} // namespace Kernel
} // namespace SputterOS

#endif // SPUTTEROS_KERNEL_SCHEDULERHEALTHMETRICS_H
