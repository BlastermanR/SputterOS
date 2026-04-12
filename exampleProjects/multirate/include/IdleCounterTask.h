#ifndef MULTIRATE_INCLUDE_IDLECOUNTERTASK_H
#define MULTIRATE_INCLUDE_IDLECOUNTERTASK_H

/**
 * @file IdleCounterTask.h
 * @brief Background task that counts its own invocations.
 *
 * Implements `IBackgroundTask` to demonstrate the SystemScheduler
 * filling gap time between Cruncher-scheduled task activations.
 * Each dispatch increments a counter and periodically logs a
 * "background alive" message to telemetry.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/11/2026
 */

#include "sputteros/osal/SputterTime.h"
#include "sputteros/osal/tasks/IBackgroundTask.h"
#include "sputteros/utils/logging/LightweightStringBuilder.h"
#include "sputteros/utils/logging/TelemetryLogger.h"
#include <cstdint>

namespace Multirate
{

/**
 * @brief Background task — runs in gap time, reports invocation count.
 *
 * The SystemScheduler dispatches this task whenever the Cruncher has
 * no READY slots remaining and gap time exceeds `kMinGapSliceUs`.
 * A 1 ms budget cap ensures it never delays the next scheduled task.
 */
class IdleCounterTask : public SputterOS::IBackgroundTask
{
  public:
    /** @brief Maximum execution budget per dispatch (µs). */
    SputterOS::SputterMicros maxBudgetUs() const override { return 1000; }

    /**
     * @brief Construct an IdleCounterTask.
     * @param telemetry Shared telemetry logger.
     */
    explicit IdleCounterTask(SputterOS::TelemetryLogger &telemetry)
        : m_telemetry(telemetry), m_dispatchCount(0), m_lastLogCount(0)
    {
    }

    void init() override
    {
        m_dispatchCount = 0;
        m_lastLogCount  = 0;
    }

    /**
     * @brief Increment dispatch counter; log every 500 invocations.
     *
     * @param systemTimeMicros Monotonic system time supplied by the scheduler.
     */
    void tick(SputterOS::SputterMicros systemTimeMicros) override
    {
        ++m_dispatchCount;

        // Log every 500 dispatches to show background utilisation.
        if ((m_dispatchCount - m_lastLogCount) >= kLogInterval)
        {
            m_lastLogCount = m_dispatchCount;

            m_builder.clear();
            m_builder.append("Background dispatches: ").append(m_dispatchCount);

            m_telemetry.log(SputterOS::TelemetryLogger::TaskID::SYSTEM, m_builder.c_str(),
                            SputterOS::TelemetryLogger::Verbosity::INFO, systemTimeMicros);
        }
    }

    /** @brief Total background dispatches since init(). */
    uint32_t dispatchCount() const { return m_dispatchCount; }

  private:
    static constexpr uint32_t kLogInterval = 500;

    SputterOS::TelemetryLogger         &m_telemetry;     /**< @brief Shared telemetry buffer. */
    SputterOS::LightweightStringBuilder m_builder;       /**< @brief Reusable message formatter. */
    uint32_t                            m_dispatchCount; /**< @brief Total dispatches. */
    uint32_t                            m_lastLogCount;  /**< @brief Dispatch count at last log. */
};

} // namespace Multirate

#endif // MULTIRATE_INCLUDE_IDLECOUNTERTASK_H
