#ifndef SPUTTEROS_KERNEL_METRICS_TASKTIMER_H
#define SPUTTEROS_KERNEL_METRICS_TASKTIMER_H

/**
 * @file TaskTimer.h
 * @brief Lightweight per-task execution timing monitor with histogram tracking.
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
    static constexpr SputterMicros kHistogramBucketWidthUs = 500;

    /**
     * @brief Set the microsecond clock source for this timer.
     * @param src: Platform microsecond clock function pointer.
     */
    void setClockSource(MicrosecondSource src) { m_clockSource = src; }

    /**
     * @brief Record the start timestamp for this task's tick.
     */
    void start() { m_start = m_clockSource ? m_clockSource() : 0; }

    /**
     * @brief Record the end timestamp and update duration statistics.
     *
     * Updates last, min, max, sum, sample count, and histogram bucket.
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
        std::size_t bucket = (kHistogramBucketWidthUs > 0)
                                 ? static_cast<std::size_t>(elapsed / kHistogramBucketWidthUs)
                                 : 0;
        if (bucket >= kHistogramBuckets)
        {
            bucket = kHistogramBuckets - 1;
        }
        ++m_histogram[bucket];
    }

    /**
     * @brief Return the duration of the most recent tick in microseconds.
     */
    SputterMicros lastDuration() const { return m_lastDuration; }

    /**
     * @brief Return the minimum tick duration since construction or last reset.
     */
    SputterMicros minDuration() const { return m_minDuration; }

    /**
     * @brief Return the maximum tick duration since construction or last reset.
     */
    SputterMicros maxDuration() const { return m_maxDuration; }

    /**
     * @brief Return the rolling average duration across all completed samples.
     * @return Average duration in microseconds, or 0.0f if no samples recorded.
     */
    float getAverageDurationUs() const
    {
        if (m_sampleCount == 0)
        {
            return 0.0f;
        }
        return static_cast<float>(m_sumDurationUs) / static_cast<float>(m_sampleCount);
    }

    /**
     * @brief Return the number of completed timing samples since last reset.
     */
    uint32_t sampleCount() const { return m_sampleCount; }

    /**
     * @brief Check whether the most recent tick exceeded a budget.
     * @param budgetUs: Allowed execution time in microseconds.
     * @return true if the last tick duration exceeded `budgetUs`.
     */
    bool isOverBudget(SputterMicros budgetUs) const { return m_lastDuration > budgetUs; }

    /**
     * @brief Return the number of recorded tick overruns.
     */
    uint32_t overrunCount() const { return m_overrunCount; }

    /**
     * @brief Return the number of recorded deadline misses.
     */
    uint32_t deadlineMissCount() const { return m_deadlineMissCount; }

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
     * @brief Access the histogram bucket array.
     * @return Pointer to the first of `kHistogramBuckets` elements.
     *         Bucket i covers the range [i*kHistogramBucketWidthUs, (i+1)*kHistogramBucketWidthUs).
     *         The last bucket also captures all values >= (kHistogramBuckets-1)*kHistogramBucketWidthUs.
     */
    const uint32_t *histogram() const { return m_histogram; }

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
     *
     * @param p: Percentile as a fraction in [0.0, 1.0] (e.g. 0.95 for p95).
     * @return Estimated duration in microseconds at the given percentile,
     *         or 0 if no samples have been recorded.
     */
    SputterMicros percentileUs(float p) const
    {
        if (m_sampleCount == 0 || p <= 0.0f)
        {
            return 0;
        }
        if (p >= 1.0f)
        {
            return m_maxDuration;
        }

        // Target sample index (1-based rank)
        float targetRank = p * static_cast<float>(m_sampleCount);
        uint32_t cumulative = 0;

        for (std::size_t i = 0; i < kHistogramBuckets; ++i)
        {
            cumulative += m_histogram[i];
            if (static_cast<float>(cumulative) >= targetRank)
            {
                // Linear interpolation within this bucket
                uint32_t prevCumulative = cumulative - m_histogram[i];
                float fraction = (m_histogram[i] > 0)
                                     ? (targetRank - static_cast<float>(prevCumulative))
                                           / static_cast<float>(m_histogram[i])
                                     : 0.0f;
                SputterMicros bucketLow = i * kHistogramBucketWidthUs;
                SputterMicros bucketHigh = (i < kHistogramBuckets - 1)
                                               ? (i + 1) * kHistogramBucketWidthUs
                                               : m_maxDuration;
                if (bucketHigh < bucketLow)
                {
                    bucketHigh = bucketLow;
                }
                return bucketLow + static_cast<SputterMicros>(
                                       fraction * static_cast<float>(bucketHigh - bucketLow));
            }
        }
        return m_maxDuration;
    }

    /**
     * @brief Reset all accumulated timing statistics.
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
    }

  private:
    MicrosecondSource m_clockSource{nullptr}; /**< @brief Injected platform clock. */
    SputterMicros     m_lastDuration{0};      /**< @brief Duration of the last tick (µs). */
    SputterMicros     m_minDuration{          /**< @brief Minimum duration since last reset (µs). */
                                     std::numeric_limits<SputterMicros>::max()};
    SputterMicros     m_maxDuration{0};       /**< @brief Peak duration since last reset (µs). */
    SputterMicros     m_start{0};             /**< @brief Start timestamp of the current tick (µs). */
    uint64_t          m_sumDurationUs{0};     /**< @brief Accumulated sum for average calculation (µs). */
    uint32_t          m_sampleCount{0};       /**< @brief Number of completed timing samples. */
    uint32_t          m_overrunCount{0};      /**< @brief Number of tick overruns recorded. */
    uint32_t          m_deadlineMissCount{0}; /**< @brief Number of deadline misses recorded. */
    uint32_t          m_histogram[kHistogramBuckets]{}; /**< @brief Duration distribution buckets. */
};

} // namespace Kernel
} // namespace SputterOS

#endif // SPUTTEROS_KERNEL_METRICS_TASKTIMER_H
