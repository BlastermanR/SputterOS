#ifndef SPUTTEROS_UTILS_QUEUEDEPTHMONITOR_H
#define SPUTTEROS_UTILS_QUEUEDEPTHMONITOR_H

/**
 * @file QueueDepthMonitor.h
 * @brief Tracks command queue occupancy over time with rolling window.
 *
 * Periodically sampled by `BackgroundDiagnosticsTask` to record the
 * current queue depth. Maintains last, max, and running average depth
 * for performance analysis.
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
#include <cstddef>
#include <cstdint>
#include <limits>

namespace SputterOS
{

class QueueDepthMonitor
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
     * @brief Record a queue depth sample.
     * @param currentDepth: Current number of elements in the queue.
     * @param nowUs: Current monotonic timestamp in µs (for window rotation).
     */
    void sample(std::size_t currentDepth, SputterMicros nowUs = 0)
    {
        m_lastDepth = currentDepth;
        if (currentDepth > m_maxDepth)
        {
            m_maxDepth = currentDepth;
        }
        m_sumDepth += currentDepth;
        ++m_sampleCount;

        // Rolling window rotation (time-based)
        if (m_windowUs > 0 && nowUs >= m_windowStartUs && (nowUs - m_windowStartUs) >= m_windowUs)
        {
            rotateWindow(nowUs);
        }
    }

    /**
     * @brief Most recent sampled depth.
     */
    std::size_t lastDepth() const { return m_lastDepth; }

    /**
     * @brief Peak depth observed in the current or last window.
     */
    std::size_t maxDepth() const
    {
        if (m_reportedSampleCount > 0)
            return m_reportedMaxDepth;
        return m_maxDepth;
    }

    /**
     * @brief Rolling average depth across all samples in the current or last window.
     * @return Average depth, or 0.0f if no samples recorded.
     */
    float averageDepth() const
    {
        if (m_reportedSampleCount > 0)
            return m_reportedAvgDepth;
        if (m_sampleCount == 0)
        {
            return 0.0f;
        }
        return static_cast<float>(m_sumDepth) / static_cast<float>(m_sampleCount);
    }

    /**
     * @brief Number of depth samples in the current or last window.
     */
    uint32_t sampleCount() const
    {
        if (m_reportedSampleCount > 0)
            return m_reportedSampleCount;
        return m_sampleCount;
    }

    /**
     * @brief Reset all tracked state and window data.
     */
    void reset()
    {
        m_lastDepth           = 0;
        m_maxDepth            = 0;
        m_sumDepth            = 0;
        m_sampleCount         = 0;
        m_windowStartUs       = 0;
        m_reportedMaxDepth    = 0;
        m_reportedAvgDepth    = 0.0f;
        m_reportedSampleCount = 0;
    }

  private:
    /**
     * @brief Snapshot current accumulators into reported fields and reset.
     * @param nowUs: Current timestamp for the new window start.
     */
    void rotateWindow(SputterMicros nowUs)
    {
        m_reportedMaxDepth    = m_maxDepth;
        m_reportedSampleCount = m_sampleCount;
        m_reportedAvgDepth =
            (m_sampleCount > 0) ? static_cast<float>(m_sumDepth) / static_cast<float>(m_sampleCount) : 0.0f;
        m_maxDepth      = 0;
        m_sumDepth      = 0;
        m_sampleCount   = 0;
        m_windowStartUs = nowUs;
    }

    // -----------------------------------------------------------------
    // Current Window Accumulators
    // -----------------------------------------------------------------
    std::size_t m_lastDepth{0};   /**< @brief Most recent depth sample. */
    std::size_t m_maxDepth{0};    /**< @brief Peak depth since reset. */
    uint64_t    m_sumDepth{0};    /**< @brief Sum of all depth samples for averaging. */
    uint32_t    m_sampleCount{0}; /**< @brief Number of samples taken. */

    // -----------------------------------------------------------------
    // Rolling Window State
    // -----------------------------------------------------------------
    SputterMicros m_windowUs{kDefaultWindowUs}; /**< @brief Window duration (µs). 0 = disabled. */
    SputterMicros m_windowStartUs{0};           /**< @brief Timestamp of current window start. */

    // -----------------------------------------------------------------
    // Reported (Last Complete Window)
    // -----------------------------------------------------------------
    std::size_t m_reportedMaxDepth{0};    /**< @brief Max depth in last window. */
    float       m_reportedAvgDepth{0.0f}; /**< @brief Avg depth in last window. */
    uint32_t    m_reportedSampleCount{0}; /**< @brief Samples in last window. */
};

} // namespace SputterOS

#endif // SPUTTEROS_UTILS_QUEUEDEPTHMONITOR_H
