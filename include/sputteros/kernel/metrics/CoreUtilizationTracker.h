#ifndef SPUTTEROS_KERNEL_METRICS_COREUTILIZATIONTRACKER_H
#define SPUTTEROS_KERNEL_METRICS_COREUTILIZATIONTRACKER_H

/**
 * @file CoreUtilizationTracker.h
 * @brief Per-core busy vs. idle utilization tracker.
 *
 * Accumulates the ratio of busy time (task execution) to total wall
 * time across consecutive `System::tick()` calls. Wall time is measured
 * as the interval between consecutive `recordTickStart()` calls,
 * capturing the full tick period including sleep/idle gaps between ticks.
 * The tracker uses a windowed accumulator that auto-resets after
 * `kWindowTicks` ticks to prevent overflow and provide a rolling
 * utilization figure.
 *
 * Instrumented by `System::tick()`:
 * - `recordTickStart()` at the beginning of each tick.
 * - `recordTickEnd()` after all tasks have been dispatched, passing
 *   the sum of individual task durations as `busyDuration`.
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

class CoreUtilizationTracker
{
  public:
    /** @brief Number of ticks per measurement window before auto-reset. */
    static constexpr uint32_t kWindowTicks = 1000;

    /**
     * @brief Record the start of a tick and accumulate wall time.
     *
     * Wall time is measured as the interval between consecutive
     * `recordTickStart()` calls, capturing the full tick period
     * including any sleep/idle gap between ticks. The first tick
     * is skipped (no previous start to measure from).
     *
     * @param now: Current monotonic system time in microseconds.
     */
    void recordTickStart(SputterMicros now)
    {
        if (m_hasPrev)
        {
            SputterMicros wallTime = (now >= m_prevTickStart) ? (now - m_prevTickStart) : 0;
            m_totalUs += wallTime;
        }
        m_prevTickStart = now;
        m_hasPrev       = true;
        m_tickStart     = now;
    }

    /**
     * @brief Record the end of a tick with the measured busy duration.
     *
     * Accumulates busy time and manages the measurement window.
     * Auto-resets the window after `kWindowTicks` ticks.
     *
     * @param busyDuration: Sum of all task execution times during this tick (µs).
     */
    void recordTickEnd(SputterMicros busyDuration)
    {
        m_busyUs += busyDuration;
        ++m_tickCount;

        if (m_tickCount >= kWindowTicks)
        {
            // Rotate: save current window as the reported values, then reset
            m_reportedBusyUs  = m_busyUs;
            m_reportedTotalUs = m_totalUs;
            m_reportedTicks   = m_tickCount;
            m_busyUs          = 0;
            m_totalUs         = 0;
            m_tickCount       = 0;
        }
    }

    /**
     * @brief Get the CPU utilization as a fraction in [0.0, 1.0].
     *
     * Returns the utilization from the most recent complete window,
     * or the current in-progress window if no complete window exists yet.
     *
     * @return Busy/total ratio, or 0.0 if no ticks recorded.
     */
    float getUtilization() const
    {
        // Prefer the last complete window; fall back to in-progress
        SputterMicros busy  = (m_reportedTotalUs > 0) ? m_reportedBusyUs : m_busyUs;
        SputterMicros total = (m_reportedTotalUs > 0) ? m_reportedTotalUs : m_totalUs;
        if (total == 0)
        {
            return 0.0f;
        }
        return static_cast<float>(busy) / static_cast<float>(total);
    }

    /**
     * @brief Get the total wall-clock time of the current/last measurement window.
     * @return Window duration in microseconds.
     */
    SputterMicros getWindowUs() const { return (m_reportedTotalUs > 0) ? m_reportedTotalUs : m_totalUs; }

    /**
     * @brief Get busy time of the current/last measurement window.
     * @return Busy duration in microseconds.
     */
    SputterMicros getBusyUs() const { return (m_reportedTotalUs > 0) ? m_reportedBusyUs : m_busyUs; }

    /**
     * @brief Get the tick count in the current/last measurement window.
     */
    uint32_t getTickCount() const { return (m_reportedTotalUs > 0) ? m_reportedTicks : m_tickCount; }

    /**
     * @brief Reset all tracking state.
     */
    void reset()
    {
        m_tickStart       = 0;
        m_prevTickStart   = 0;
        m_hasPrev         = false;
        m_busyUs          = 0;
        m_totalUs         = 0;
        m_tickCount       = 0;
        m_reportedBusyUs  = 0;
        m_reportedTotalUs = 0;
        m_reportedTicks   = 0;
    }

  private:
    SputterMicros m_tickStart{0};       /**< @brief Start timestamp of current tick. */
    SputterMicros m_prevTickStart{0};   /**< @brief Start timestamp of previous tick (for wall time). */
    bool          m_hasPrev{false};     /**< @brief True after the first recordTickStart() call. */
    SputterMicros m_busyUs{0};          /**< @brief Accumulated busy time in current window (µs). */
    SputterMicros m_totalUs{0};         /**< @brief Accumulated wall time in current window (µs). */
    uint32_t      m_tickCount{0};       /**< @brief Ticks in current window. */
    SputterMicros m_reportedBusyUs{0};  /**< @brief Busy time from last complete window (µs). */
    SputterMicros m_reportedTotalUs{0}; /**< @brief Wall time from last complete window (µs). */
    uint32_t      m_reportedTicks{0};   /**< @brief Tick count from last complete window. */
};

} // namespace Kernel
} // namespace SputterOS

#endif // SPUTTEROS_KERNEL_METRICS_COREUTILIZATIONTRACKER_H
