#ifndef LIFECYCLE_INCLUDE_WORKERTASK_H
#define LIFECYCLE_INCLUDE_WORKERTASK_H

/**
 * @file WorkerTask.h
 * @brief Scheduled task on Core 0 that counts iterations and reacts
 *        to suspend/resume lifecycle events.
 *
 * Runs at 5 Hz (200 ms period).  Logs its iteration count and
 * responds to the `onSuspend()` / `onResume()` callbacks to
 * demonstrate how scheduled tasks participate in kernel lifecycle
 * transitions.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/11/2026
 */

#include "sputteros/osal/SputterTime.h"
#include "sputteros/osal/tasks/IScheduledTask.h"
#include "sputteros/utils/logging/LightweightStringBuilder.h"
#include "sputteros/utils/logging/TelemetryLogger.h"
#include <cstdint>

namespace Lifecycle
{

/**
 * @brief Core 0 periodic worker — counts ticks, logs lifecycle events.
 */
class WorkerTask : public SputterOS::IScheduledTask
{
  public:
    /** @brief Work period (200 ms = 5 Hz). */
    static constexpr SputterOS::SputterMicros kPeriodUs = 200'000;

    SputterOS::SputterMicros periodUs() const override { return kPeriodUs; }

    /**
     * @brief Construct a WorkerTask.
     * @param telemetry Shared telemetry logger.
     */
    explicit WorkerTask(SputterOS::TelemetryLogger &telemetry)
        : m_telemetry(telemetry), m_iterCount(0), m_lastTick(0)
    {
    }

    void init() override
    {
        m_iterCount = 0;
        m_lastTick  = 0;
    }

    /**
     * @brief Increment iteration counter and log.
     *
     * Rate-limited internally to `kPeriodUs`.
     *
     * @param systemTimeMicros Monotonic time from the scheduler.
     */
    void tick(SputterOS::SputterMicros systemTimeMicros) override
    {
        if ((systemTimeMicros - m_lastTick) < kPeriodUs) return;
        m_lastTick = systemTimeMicros;

        ++m_iterCount;

        m_builder.clear();
        m_builder.append("[Core 0] Worker tick #").append(m_iterCount);

        m_telemetry.log(SputterOS::TelemetryLogger::TaskID::CONTROL,
                        m_builder.c_str(),
                        SputterOS::TelemetryLogger::Verbosity::STATUS,
                        systemTimeMicros);
    }

    /**
     * @brief Called by the Cruncher when the kernel transitions to SUSPENDING.
     */
    void onSuspend() override
    {
        m_builder.clear();
        m_builder.append("[Core 0] Worker SUSPENDED at tick #").append(m_iterCount);

        m_telemetry.log(SputterOS::TelemetryLogger::TaskID::CONTROL,
                        m_builder.c_str(),
                        SputterOS::TelemetryLogger::Verbosity::STATUS, 0);
    }

    /**
     * @brief Called by the Cruncher when the kernel transitions back to RUNNING.
     */
    void onResume() override
    {
        m_builder.clear();
        m_builder.append("[Core 0] Worker RESUMED at tick #").append(m_iterCount);

        m_telemetry.log(SputterOS::TelemetryLogger::TaskID::CONTROL,
                        m_builder.c_str(),
                        SputterOS::TelemetryLogger::Verbosity::STATUS, 0);
    }

    /** @brief Total tick() invocations. */
    uint32_t iterCount() const { return m_iterCount; }

  private:
    SputterOS::TelemetryLogger         &m_telemetry; /**< @brief Telemetry logger. */
    SputterOS::LightweightStringBuilder m_builder;    /**< @brief Message formatter. */
    uint32_t                            m_iterCount;  /**< @brief Running tick count. */
    SputterOS::SputterMicros            m_lastTick;   /**< @brief Last rate-limited tick (µs). */
};

} // namespace Lifecycle

#endif // LIFECYCLE_INCLUDE_WORKERTASK_H
