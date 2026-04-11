#ifndef MULTIRATE_INCLUDE_FASTSAMPLETASK_H
#define MULTIRATE_INCLUDE_FASTSAMPLETASK_H

/**
 * @file FastSampleTask.h
 * @brief High-frequency scheduled task that simulates sensor sampling.
 *
 * Runs at 100 Hz (10 ms period) to demonstrate the Cruncher
 * dispatching a fast-rate task alongside slower ones.  Each tick
 * reads a simulated sensor value (a simple counter) and accumulates
 * a running total.  The accumulated data is periodically consumed
 * by `SlowReportTask`.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/11/2026
 */

#include "sputteros/osal/SputterTime.h"
#include "sputteros/osal/tasks/IScheduledTask.h"
#include <atomic>
#include <cstdint>

namespace Multirate
{

/**
 * @brief Simulated high-frequency sensor sampler at 100 Hz.
 *
 * Shares `m_sampleCount` and `m_accumulator` with `SlowReportTask`
 * via direct pointer access.  Both tasks run on Core 0 (single-core),
 * so concurrent access is impossible — no atomics needed.
 */
class FastSampleTask : public SputterOS::IScheduledTask
{
  public:
    /** @brief Sampling period in microseconds (10 ms = 100 Hz). */
    static constexpr SputterOS::SputterMicros kPeriodUs = 10'000;

    /** @brief The task's activation period in microseconds. */
    SputterOS::SputterMicros periodUs() const override { return kPeriodUs; }

    /** @brief Declared worst-case execution time (trivial work). */
    SputterOS::SputterMicros declaredWcetUs() const override { return 50; }

    FastSampleTask() = default;

    void init() override
    {
        m_sampleCount = 0;
        m_accumulator = 0;
        m_lastTick    = 0;
    }

    /**
     * @brief Simulate reading a sensor and accumulating samples.
     *
     * Rate-limited internally to `kPeriodUs` via timestamp comparison.
     * Once the Cruncher is implemented (Phase 2), the internal gate
     * becomes redundant but harmless.
     *
     * @param systemTimeMicros Monotonic system time supplied by the scheduler.
     */
    void tick(SputterOS::SputterMicros systemTimeMicros) override
    {
        if ((systemTimeMicros - m_lastTick) < kPeriodUs)
            return;
        m_lastTick = systemTimeMicros;

        // Simulate a sensor reading that oscillates 0..99
        const uint32_t reading = m_sampleCount % 100;
        m_accumulator += reading;
        ++m_sampleCount;
    }

    /** @brief Total samples taken since init(). */
    uint32_t sampleCount() const { return m_sampleCount; }

    /** @brief Running sum of all simulated sensor readings. */
    uint64_t accumulator() const { return m_accumulator; }

    /**
     * @brief Reset the accumulator and return its previous value.
     * @return The accumulator value before reset.
     */
    uint64_t drainAccumulator()
    {
        const uint64_t val = m_accumulator;
        m_accumulator      = 0;
        return val;
    }

  private:
    uint32_t                 m_sampleCount{0}; /**< @brief Total samples taken. */
    uint64_t                 m_accumulator{0}; /**< @brief Running sum of readings. */
    SputterOS::SputterMicros m_lastTick{0};    /**< @brief Last tick timestamp (µs). */
};

} // namespace Multirate

#endif // MULTIRATE_INCLUDE_FASTSAMPLETASK_H
