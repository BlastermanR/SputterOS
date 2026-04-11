#ifndef SPUTTEROS_KERNEL_TASKTIMER_H
#define SPUTTEROS_KERNEL_TASKTIMER_H

/**
 * @file TaskTimer.h
 * @brief Lightweight per-task execution timing monitor.
 *
 * Each `ITask` owns a `TaskTimer` instance that the `System` starts
 * and stops around every `tick()` call. The `DiagnosticsTask` reads
 * all task timers each cycle to detect budget violations and aggregate
 * timing statistics.
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

namespace SputterOS
{
namespace Kernel
{

class TaskTimer
{
  public:
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
        m_sumDurationUs += elapsed;
        ++m_sampleCount;
    }

    /**
     * @brief Return the duration of the most recent tick in microseconds.
     */
    SputterMicros lastDuration() const { return m_lastDuration; }

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

    /**
     * @brief Reset all accumulated timing statistics.
     */
    void reset()
    {
        m_lastDuration      = 0;
        m_maxDuration       = 0;
        m_sumDurationUs     = 0;
        m_sampleCount       = 0;
        m_overrunCount      = 0;
        m_deadlineMissCount = 0;
    }

  private:
    MicrosecondSource m_clockSource{nullptr}; /**< @brief Injected platform clock. */
    SputterMicros     m_lastDuration{0};      /**< @brief Duration of the last tick (µs). */
    SputterMicros     m_maxDuration{0};       /**< @brief Peak duration since last reset (µs). */
    SputterMicros     m_start{0};             /**< @brief Start timestamp of the current tick (µs). */
    uint64_t          m_sumDurationUs{0};     /**< @brief Accumulated sum for average calculation (µs). */
    uint32_t          m_sampleCount{0};       /**< @brief Number of completed timing samples. */
    uint32_t          m_overrunCount{0};      /**< @brief Number of tick overruns recorded. */
    uint32_t          m_deadlineMissCount{0}; /**< @brief Number of deadline misses recorded. */
};

} // namespace Kernel
} // namespace SputterOS

#endif // SPUTTEROS_KERNEL_TASKTIMER_H
