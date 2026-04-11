#ifndef MULTIRATE_INCLUDE_SLOWREPORTTASK_H
#define MULTIRATE_INCLUDE_SLOWREPORTTASK_H

/**
 * @file SlowReportTask.h
 * @brief Low-frequency scheduled task that reports accumulated samples.
 *
 * Runs at 2 Hz (500 ms period) to demonstrate multi-rate scheduling.
 * Each tick drains the accumulator from `FastSampleTask` and logs a
 * summary to `TelemetryLogger`, showing how many fast samples were
 * collected between reports.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/11/2026
 */

#include "FastSampleTask.h"
#include "sputteros/osal/SputterTime.h"
#include "sputteros/osal/tasks/IScheduledTask.h"
#include "sputteros/utils/logging/LightweightStringBuilder.h"
#include "sputteros/utils/logging/TelemetryLogger.h"
#include <cstdint>

namespace Multirate
{

/**
 * @brief Periodic reporter at 2 Hz — consumes data from FastSampleTask.
 *
 * Demonstrates the Cruncher running a slow task alongside a fast task
 * on the same core.  Priority is auto-assigned by RMS ordering
 * (FastSampleTask gets higher priority due to shorter period).
 */
class SlowReportTask : public SputterOS::IScheduledTask
{
  public:
    /** @brief Report period in microseconds (500 ms = 2 Hz). */
    static constexpr SputterOS::SputterMicros kPeriodUs = 500'000;

    /** @brief The task's activation period in microseconds. */
    SputterOS::SputterMicros periodUs() const override { return kPeriodUs; }

    /**
     * @brief Construct a SlowReportTask.
     * @param telemetry Shared telemetry logger — lifetime must exceed this task.
     * @param sampler   The fast-rate sampler to drain data from.
     */
    SlowReportTask(SputterOS::TelemetryLogger &telemetry, FastSampleTask &sampler)
        : m_telemetry(telemetry), m_sampler(sampler), m_reportCount(0)
    {
    }

    void init() override
    {
        m_reportCount = 0;
        m_lastTick    = 0;
    }

    /**
     * @brief Drain accumulated samples and log a summary.
     *
     * Rate-limited internally to `kPeriodUs`.  Once the Cruncher is
     * implemented, internal gating becomes redundant but harmless.
     *
     * @param systemTimeMicros Monotonic system time supplied by the scheduler.
     */
    void tick(SputterOS::SputterMicros systemTimeMicros) override
    {
        if ((systemTimeMicros - m_lastTick) < kPeriodUs) return;
        m_lastTick = systemTimeMicros;
        ++m_reportCount;

        const uint64_t accum   = m_sampler.drainAccumulator();
        const uint32_t samples = m_sampler.sampleCount();

        m_builder.clear();
        m_builder.append("Report #")
            .append(static_cast<uint32_t>(m_reportCount))
            .append(" | samples=")
            .append(samples)
            .append(" accum=")
            .append(static_cast<uint32_t>(accum & 0xFFFFFFFF));

        m_telemetry.log(SputterOS::TelemetryLogger::TaskID::SYSTEM, m_builder.c_str(),
                        SputterOS::TelemetryLogger::Verbosity::STATUS, systemTimeMicros);
    }

  private:
    SputterOS::TelemetryLogger         &m_telemetry;  /**< @brief Shared telemetry buffer. */
    FastSampleTask                     &m_sampler;     /**< @brief Data source (fast task). */
    SputterOS::LightweightStringBuilder m_builder;     /**< @brief Reusable message formatter. */
    uint32_t                            m_reportCount; /**< @brief Running report count. */
    SputterOS::SputterMicros            m_lastTick{0}; /**< @brief Last tick timestamp (µs). */
};

} // namespace Multirate

#endif // MULTIRATE_INCLUDE_SLOWREPORTTASK_H
