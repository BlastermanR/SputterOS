#ifndef SPUTTEROS_KERNEL_METRICS_TASKTIMER_H
#define SPUTTEROS_KERNEL_METRICS_TASKTIMER_H

/**
 * @file TaskTimer.h
 * @brief Lightweight per-task execution timing monitor with histogram tracking
 *        and time-based rolling window.
 *
 * Each `ITask` owns a `TaskTimer` instance that the `System` starts
 * and stops around every `tick()` call. The `DiagnosticsTask` reads
 * all task timers each cycle to detect budget violations and aggregate
 * timing statistics.
 *
 * In addition to last/max/average tracking, `TaskTimer` maintains a
 * fixed-bucket histogram of tick durations for distribution analysis.
 * Bucket boundaries are defined at compile time by `kHistogramBuckets`
 * and `kHistogramBucketWidthUs`. The `percentileUs()` method walks the
 * histogram to approximate arbitrary percentiles (p50, p95, p99, etc.).
 *
 * A time-based **rolling window** prevents counter overflow in
 * high-frequency schedulers. When the elapsed time since the window
 * start exceeds `metricsWindowUs()`, accumulators are snapshotted into
 * "reported" fields and reset. Accessors return the last complete
 * window (or the in-progress window if no rotation has occurred yet).
 * The window duration is set via `setMetricsWindowUs()`, propagated
 * by `SystemBuilder` from `CfgMetricsWindowUs<Cfg>`.
 *
 * `TaskTimer` uses an injectable `MicrosecondSource` function pointer
 * for clock access, avoiding any dependency on `std::chrono` or
 * platform-specific timer APIs. The clock source is propagated by
 * `SystemBuilder` during `build()`.
 *
 * @note Intended only for System-managed instrumentation.
 *       Users should not call `start()` / `stop()` manually.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/9/2026
 */

#include "sputteros/osal/SputterTime.h"
#include <cstdint>
#include <limits>

namespace SputterOS
{
namespace Kernel
{

class TaskTimer
{
  public:
    /** @brief Number of histogram buckets for duration distribution tracking. */
    static constexpr std::size_t kHistogramBuckets = 8;

    /** @brief Width of each histogram bucket in microseconds. */
    static constexpr SputterMicros kHistogramBucketWidthUs = 512;

    /** @brief Default metrics window duration (60 seconds). */
    static constexpr SputterMicros kDefaultWindowUs = 60'000'000;

    /**
     * @brief Set the microsecond clock source for this timer.
     * @param src: Platform microsecond clock function pointer.
     */
    void setClockSource(MicrosecondSource src) { m_clockSource = src; }

    /**
     * @brief Set the rolling metrics window duration.
     * @param windowUs: Window length in microseconds. 0 disables windowing.
     */
    void setMetricsWindowUs(SputterMicros windowUs) { m_windowUs = windowUs; }

    /**
     * @brief Get the configured metrics window duration.
     * @return Window length in microseconds.
     */
    SputterMicros metricsWindowUs() const { return m_windowUs; }

    /**
     * @brief Record the start timestamp for this task's tick.
     */
    void start() { m_start = m_clockSource ? m_clockSource() : 0; }

    /**
     * @brief Record the end timestamp and update duration statistics.
     *
     * Updates last, min, max, sum, sample count, and histogram bucket.
     * Rotates the rolling window when elapsed time exceeds the configured
     * window duration.
     */
    void stop()
    {
        SputterMicros now     = m_clockSource ? m_clockSource() : 0;
        SputterMicros elapsed = (now >= m_start) ? (now - m_start) : 0;
        m_lastDuration        = elapsed;
        if (elapsed > m_maxDuration)
        {
            m_maxDuration = elapsed;
        }
        if (elapsed < m_minDuration)
        {
            m_minDuration = elapsed;
        }
        m_sumDurationUs += elapsed;
        ++m_sampleCount;

        // Histogram: bucket index = elapsed / bucketWidth, clamped to last bucket
        std::size_t bucket =
            (kHistogramBucketWidthUs > 0) ? static_cast<std::size_t>(elapsed / kHistogramBucketWidthUs) : 0;
        if (bucket >= kHistogramBuckets)
        {
            bucket = kHistogramBuckets - 1;
        }
        ++m_histogram[bucket];

        // Rolling window rotation (time-based)
        if (m_windowUs > 0 && now >= m_windowStartUs && (now - m_windowStartUs) >= m_windowUs)
        {
            rotateWindow(now);
        }
    }

    /**
     * @brief Return the duration of the most recent tick in microseconds.
     */
    SputterMicros lastDuration() const { return m_lastDuration; }

    /**
     * @brief Return the minimum tick duration in the current or last window.
     */
    SputterMicros minDuration() const
    {
        if (m_reportedSampleCount > 0)
            return m_reportedMinDuration;
        return m_minDuration;
    }

    /**
     * @brief Return the maximum tick duration in the current or last window.
     */
    SputterMicros maxDuration() const
    {
        if (m_reportedSampleCount > 0)
            return m_reportedMaxDuration;
        return m_maxDuration;
    }

    /**
     * @brief Return the rolling average duration across all completed samples
     *        in the current or last window.
     * @return Average duration in microseconds, or 0.0f if no samples recorded.
     */
    float getAverageDurationUs() const
    {
        if (m_reportedSampleCount > 0)
            return m_reportedAvgUs;
        if (m_sampleCount == 0)
        {
            return 0.0f;
        }
        return static_cast<float>(m_sumDurationUs) / static_cast<float>(m_sampleCount);
    }

    /**
     * @brief Return the number of completed timing samples in the
     *        current or last window.
     */
    uint32_t sampleCount() const
    {
        if (m_reportedSampleCount > 0)
            return m_reportedSampleCount;
        return m_sampleCount;
    }

    /**
     * @brief Check whether the most recent tick exceeded a budget.
     * @param budgetUs: Allowed execution time in microseconds.
     * @return true if the last tick duration exceeded `budgetUs`.
     */
    bool isOverBudget(SputterMicros budgetUs) const { return m_lastDuration > budgetUs; }

    /**
     * @brief Return the number of recorded tick overruns in the
     *        current or last window.
     */
    uint32_t overrunCount() const
    {
        if (m_reportedSampleCount > 0)
            return m_reportedOverruns;
        return m_overrunCount;
    }

    /**
     * @brief Return the number of recorded deadline misses in the
     *        current or last window.
     */
    uint32_t deadlineMissCount() const
    {
        if (m_reportedSampleCount > 0)
            return m_reportedMisses;
        return m_deadlineMissCount;
    }

    /**
     * @brief Increment the overrun counter by one.
     */
    void recordOverrun() { ++m_overrunCount; }

    /**
     * @brief Increment the deadline-miss counter by one.
     */
    void recordDeadlineMiss() { ++m_deadlineMissCount; }

    // -----------------------------------------------------------------
    // Histogram Distribution API
    // -----------------------------------------------------------------

    /**
     * @brief Access the histogram bucket array for the current or last window.
     * @return Pointer to the first of `kHistogramBuckets` elements.
     *         Bucket i covers the range [i*kHistogramBucketWidthUs, (i+1)*kHistogramBucketWidthUs).
     *         The last bucket also captures all values >= (kHistogramBuckets-1)*kHistogramBucketWidthUs.
     */
    const uint32_t *histogram() const
    {
        if (m_reportedSampleCount > 0)
            return m_reportedHistogram;
        return m_histogram;
    }

    /**
     * @brief Number of histogram buckets.
     */
    static constexpr std::size_t histogramBucketCount() { return kHistogramBuckets; }

    /**
     * @brief Width of each histogram bucket in microseconds.
     */
    static constexpr SputterMicros histogramBucketWidthUs() { return kHistogramBucketWidthUs; }

    /**
     * @brief Approximate the Nth percentile from the histogram.
     *
     * Walks histogram buckets to find the bucket containing the target
     * sample, then linearly interpolates within that bucket.
     * Uses the reported window if available, otherwise the in-progress window.
     *
     * @param p: Percentile as a fraction in [0.0, 1.0] (e.g. 0.95 for p95).
     * @return Estimated duration in microseconds at the given percentile,
     *         or 0 if no samples have been recorded.
     */
    SputterMicros percentileUs(float p) const
    {
        uint32_t        samples = sampleCount();
        SputterMicros   maxDur  = maxDuration();
        const uint32_t *hist    = histogram();

        if (samples == 0 || p <= 0.0f)
        {
            return 0;
        }
        if (p >= 1.0f)
        {
            return maxDur;
        }

        // Target sample index (1-based rank)
        float    targetRank = p * static_cast<float>(samples);
        uint32_t cumulative = 0;

        for (std::size_t i = 0; i < kHistogramBuckets; ++i)
        {
            cumulative += hist[i];
            if (static_cast<float>(cumulative) >= targetRank)
            {
                // Linear interpolation within this bucket
                uint32_t      prevCumulative = cumulative - hist[i];
                float         fraction       = (hist[i] > 0)
                                                   ? (targetRank - static_cast<float>(prevCumulative)) / static_cast<float>(hist[i])
                                                   : 0.0f;
                SputterMicros bucketLow      = i * kHistogramBucketWidthUs;
                SputterMicros bucketHigh     = (i < kHistogramBuckets - 1) ? (i + 1) * kHistogramBucketWidthUs : maxDur;
                if (bucketHigh < bucketLow)
                {
                    bucketHigh = bucketLow;
                }
                return bucketLow + static_cast<SputterMicros>(fraction * static_cast<float>(bucketHigh - bucketLow));
            }
        }
        return maxDur;
    }

    /**
     * @brief Reset all accumulated timing statistics and window state.
     */
    void reset()
    {
        m_lastDuration      = 0;
        m_minDuration       = std::numeric_limits<SputterMicros>::max();
        m_maxDuration       = 0;
        m_sumDurationUs     = 0;
        m_sampleCount       = 0;
        m_overrunCount      = 0;
        m_deadlineMissCount = 0;
        for (std::size_t i = 0; i < kHistogramBuckets; ++i)
        {
            m_histogram[i] = 0;
        }
        m_windowStartUs       = 0;
        m_reportedSampleCount = 0;
        m_reportedMinDuration = std::numeric_limits<SputterMicros>::max();
        m_reportedMaxDuration = 0;
        m_reportedAvgUs       = 0.0f;
        m_reportedOverruns    = 0;
        m_reportedMisses      = 0;
        for (std::size_t i = 0; i < kHistogramBuckets; ++i)
        {
            m_reportedHistogram[i] = 0;
        }
    }

  private:
    /**
     * @brief Snapshot current accumulators into reported fields and reset.
     * @param now: Current timestamp for the new window start.
     */
    void rotateWindow(SputterMicros now)
    {
        m_reportedSampleCount = m_sampleCount;
        m_reportedMinDuration = m_minDuration;
        m_reportedMaxDuration = m_maxDuration;
        m_reportedAvgUs =
            (m_sampleCount > 0) ? static_cast<float>(m_sumDurationUs) / static_cast<float>(m_sampleCount) : 0.0f;
        m_reportedOverruns = m_overrunCount;
        m_reportedMisses   = m_deadlineMissCount;
        for (std::size_t i = 0; i < kHistogramBuckets; ++i)
        {
            m_reportedHistogram[i] = m_histogram[i];
            m_histogram[i]         = 0;
        }
        m_minDuration       = std::numeric_limits<SputterMicros>::max();
        m_maxDuration       = 0;
        m_sumDurationUs     = 0;
        m_sampleCount       = 0;
        m_overrunCount      = 0;
        m_deadlineMissCount = 0;
        m_windowStartUs     = now;
    }

    // -----------------------------------------------------------------
    // Clock & Measurement State
    // -----------------------------------------------------------------
    MicrosecondSource m_clockSource{nullptr}; /**< @brief Injected platform clock. */
    SputterMicros     m_lastDuration{0};      /**< @brief Duration of the last tick (µs). */
    SputterMicros     m_minDuration{          /**< @brief Minimum duration since last reset (µs). */
                                std::numeric_limits<SputterMicros>::max()};
    SputterMicros     m_maxDuration{0};                 /**< @brief Peak duration since last reset (µs). */
    SputterMicros     m_start{0};                       /**< @brief Start timestamp of the current tick (µs). */
    uint64_t          m_sumDurationUs{0};               /**< @brief Accumulated sum for average calculation (µs). */
    uint32_t          m_sampleCount{0};                 /**< @brief Number of completed timing samples. */
    uint32_t          m_overrunCount{0};                /**< @brief Number of tick overruns recorded. */
    uint32_t          m_deadlineMissCount{0};           /**< @brief Number of deadline misses recorded. */
    uint32_t          m_histogram[kHistogramBuckets]{}; /**< @brief Duration distribution buckets. */

    // -----------------------------------------------------------------
    // Rolling Window State
    // -----------------------------------------------------------------
    SputterMicros m_windowUs{kDefaultWindowUs}; /**< @brief Window duration (µs). 0 = disabled. */
    SputterMicros m_windowStartUs{0};           /**< @brief Timestamp of current window start. */

    // -----------------------------------------------------------------
    // Reported (Last Complete Window)
    // -----------------------------------------------------------------
    uint32_t      m_reportedSampleCount{0}; /**< @brief Samples in last complete window. */
    SputterMicros m_reportedMinDuration{    /**< @brief Min duration in last window. */
                                        std::numeric_limits<SputterMicros>::max()};
    SputterMicros m_reportedMaxDuration{0};                 /**< @brief Max duration in last window. */
    float         m_reportedAvgUs{0.0f};                    /**< @brief Avg duration in last window. */
    uint32_t      m_reportedOverruns{0};                    /**< @brief Overruns in last window. */
    uint32_t      m_reportedMisses{0};                      /**< @brief Deadline misses in last window. */
    uint32_t      m_reportedHistogram[kHistogramBuckets]{}; /**< @brief Histogram from last window. */
};

} // namespace Kernel
} // namespace SputterOS

#endif // SPUTTEROS_KERNEL_METRICS_TASKTIMER_H
