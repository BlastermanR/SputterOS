#ifndef SPUTTEROS_KERNEL_METRICS_SCHEDULERHEALTHMETRICS_H
#define SPUTTEROS_KERNEL_METRICS_SCHEDULERHEALTHMETRICS_H

/**
 * @file SchedulerHealthMetrics.h
 * @brief Aggregate scheduler health statistics: gap time, overruns, deadline misses.
 *
 * Tracks cumulative and peak gap time (idle time between task dispatches),
 * total overrun and deadline miss counts aggregated across all monitored
 * tasks, and total tick count. Gap time is recorded by `System::tick()`
 * as wall-clock time minus total busy time each tick.
 *
 * A time-based **rolling window** prevents counter overflow in
 * high-frequency schedulers. When the elapsed time since the window
 * start exceeds the configured window duration, accumulators are
 * snapshotted and reset. Accessors return the last complete window
 * (or the in-progress window if no rotation has occurred yet).
 *
 * @note Zero-heap, header-only. No dynamic allocation.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/11/2026
 */

#include "sputteros/osal/SputterTime.h"
#include <cstdint>
#include <limits>

namespace SputterOS
{
namespace Kernel
{

class SchedulerHealthMetrics
{
  public:
    /** @brief Default metrics window duration (60 seconds). */
    static constexpr SputterMicros kDefaultWindowUs = 60'000'000;

    /**
     * @brief Set the rolling metrics window duration.
     * @param windowUs: Window length in microseconds. 0 disables windowing.
     */
    void setMetricsWindowUs(SputterMicros windowUs) { m_windowUs = windowUs; }

    /**
     * @brief Record one tick's gap time.
     * @param gapUs: Idle time in this tick (wall time - busy time) in µs.
     * @param nowUs: Current monotonic timestamp in µs (for window rotation).
     */
    void recordGap(SputterMicros gapUs, SputterMicros nowUs = 0)
    {
        if (gapUs > m_maxGapUs)
        {
            m_maxGapUs = gapUs;
        }
        m_totalGapUs += gapUs;
        ++m_tickCount;

        // Rolling window rotation (time-based)
        if (m_windowUs > 0 && nowUs >= m_windowStartUs && (nowUs - m_windowStartUs) >= m_windowUs)
        {
            rotateWindow(nowUs);
        }
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
     * @brief Total gap (idle) time in the current or last window.
     */
    SputterMicros totalGapUs() const
    {
        if (m_reportedTickCount > 0)
            return m_reportedTotalGapUs;
        return m_totalGapUs;
    }

    /**
     * @brief Peak gap time observed in the current or last window.
     */
    SputterMicros maxGapUs() const
    {
        if (m_reportedTickCount > 0)
            return m_reportedMaxGapUs;
        return m_maxGapUs;
    }

    /**
     * @brief Average gap time per tick in the current or last window.
     * @return Average gap in microseconds, or 0 if no ticks recorded.
     */
    float averageGapUs() const
    {
        if (m_reportedTickCount > 0)
            return m_reportedAvgGapUs;
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
     * @brief Total number of ticks in the current or last window.
     */
    uint32_t tickCount() const
    {
        if (m_reportedTickCount > 0)
            return m_reportedTickCount;
        return m_tickCount;
    }

    /**
     * @brief Reset all metrics and window state.
     */
    void reset()
    {
        m_totalGapUs          = 0;
        m_maxGapUs            = 0;
        m_tickCount           = 0;
        m_totalOverruns       = 0;
        m_totalDeadlineMisses = 0;
        m_windowStartUs       = 0;
        m_reportedTotalGapUs  = 0;
        m_reportedMaxGapUs    = 0;
        m_reportedAvgGapUs    = 0.0f;
        m_reportedTickCount   = 0;
    }

  private:
    /**
     * @brief Snapshot current accumulators into reported fields and reset.
     * @param nowUs: Current timestamp for the new window start.
     */
    void rotateWindow(SputterMicros nowUs)
    {
        m_reportedTotalGapUs = m_totalGapUs;
        m_reportedMaxGapUs   = m_maxGapUs;
        m_reportedTickCount  = m_tickCount;
        m_reportedAvgGapUs =
            (m_tickCount > 0) ? static_cast<float>(m_totalGapUs) / static_cast<float>(m_tickCount) : 0.0f;
        m_totalGapUs    = 0;
        m_maxGapUs      = 0;
        m_tickCount     = 0;
        m_windowStartUs = nowUs;
    }

    // -----------------------------------------------------------------
    // Current Window Accumulators
    // -----------------------------------------------------------------
    SputterMicros m_totalGapUs{0};          /**< @brief Cumulative gap time (µs). */
    SputterMicros m_maxGapUs{0};            /**< @brief Peak single-tick gap (µs). */
    uint32_t      m_tickCount{0};           /**< @brief Total ticks tracked. */
    uint32_t      m_totalOverruns{0};       /**< @brief Aggregate overruns from all tasks. */
    uint32_t      m_totalDeadlineMisses{0}; /**< @brief Aggregate deadline misses from all tasks. */

    // -----------------------------------------------------------------
    // Rolling Window State
    // -----------------------------------------------------------------
    SputterMicros m_windowUs{kDefaultWindowUs}; /**< @brief Window duration (µs). 0 = disabled. */
    SputterMicros m_windowStartUs{0};           /**< @brief Timestamp of current window start. */

    // -----------------------------------------------------------------
    // Reported (Last Complete Window)
    // -----------------------------------------------------------------
    SputterMicros m_reportedTotalGapUs{0};  /**< @brief Total gap in last window. */
    SputterMicros m_reportedMaxGapUs{0};    /**< @brief Max gap in last window. */
    float         m_reportedAvgGapUs{0.0f}; /**< @brief Avg gap in last window. */
    uint32_t      m_reportedTickCount{0};   /**< @brief Ticks in last window. */
};

} // namespace Kernel
} // namespace SputterOS

#endif // SPUTTEROS_KERNEL_METRICS_SCHEDULERHEALTHMETRICS_H
