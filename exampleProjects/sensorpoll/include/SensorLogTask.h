#ifndef SENSORPOLL_INCLUDE_SENSORLOGTASK_H
#define SENSORPOLL_INCLUDE_SENSORLOGTASK_H

/**
 * @file SensorLogTask.h
 * @brief Background task that periodically logs sensor statistics.
 *
 * Runs in SystemScheduler gap time.  On each dispatch, checks whether
 * enough new readings have accumulated and, if so, logs a summary.
 * Demonstrates a background task consuming data produced by a
 * scheduled task.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/11/2026
 */

#include "AdcPollTask.h"
#include "sputteros/osal/SputterTime.h"
#include "sputteros/osal/tasks/IBackgroundTask.h"
#include "sputteros/utils/logging/LightweightStringBuilder.h"
#include "sputteros/utils/logging/TelemetryLogger.h"
#include <cstdint>

namespace SensorPoll
{

/**
 * @brief Background logger — reports ADC reading count in gap time.
 */
class SensorLogTask : public SputterOS::IBackgroundTask
{
  public:
    /** @brief Max execution budget per dispatch (µs). */
    SputterOS::SputterMicros maxBudgetUs() const override { return 500; }

    /**
     * @brief Construct a SensorLogTask.
     * @param telemetry Shared telemetry logger.
     * @param poller    The ADC polling task to inspect.
     */
    SensorLogTask(SputterOS::TelemetryLogger &telemetry, AdcPollTask &poller)
        : m_telemetry(telemetry), m_poller(poller), m_lastReported(0)
    {
    }

    void init() override { m_lastReported = 0; }

    /**
     * @brief If new readings have accumulated, log a summary.
     * @param systemTimeMicros Monotonic time from the scheduler.
     */
    void tick(SputterOS::SputterMicros systemTimeMicros) override
    {
        const uint32_t current = m_poller.readingCount();
        if (current > m_lastReported && (current - m_lastReported) >= kLogThreshold)
        {
            m_lastReported = current;

            m_builder.clear();
            m_builder.append("Sensor log: ")
                .append(current)
                .append(" readings, last=")
                .append(m_poller.lastReading())
                .append(" mV");

            m_telemetry.log(SputterOS::TelemetryLogger::TaskID::SYSTEM, m_builder.c_str(),
                            SputterOS::TelemetryLogger::Verbosity::STATUS, systemTimeMicros);
        }
    }

  private:
    static constexpr uint32_t kLogThreshold = 5; /**< @brief Readings between logs. */

    SputterOS::TelemetryLogger         &m_telemetry;    /**< @brief Telemetry logger. */
    AdcPollTask                        &m_poller;       /**< @brief Data source. */
    SputterOS::LightweightStringBuilder m_builder;      /**< @brief Message formatter. */
    uint32_t                            m_lastReported; /**< @brief Count at last log. */
};

} // namespace SensorPoll

#endif // SENSORPOLL_INCLUDE_SENSORLOGTASK_H
