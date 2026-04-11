#ifndef HEARTBEAT_SRC_PULSETASK_H
#define HEARTBEAT_SRC_PULSETASK_H

/**
 * @file PulseTask.h
 * @brief Custom fourth task that emits a periodic "Pulse" heartbeat message.
 *
 * Implements `SputterOS::IScheduledTask` to participate in the single-core main
 * loop alongside the three kernel tasks (`ScheduledControlTask`, `ScheduledCommsTask`,
 * `BackgroundDiagnosticsTask`).
 *
 * Every `kPulseIntervalUs` microseconds, `PulseTask` logs the message
 * "Pulse" to the shared `TelemetryLogger` under the `SYSTEM` task tag.
 * On the OS-native platform the main loop drains `TelemetryLogger` to
 * `StdoutStreamReader` each cycle, making the message visible in the
 * terminal.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/8/2026
 */

#include "sputteros/osal/SputterTime.h"
#include "sputteros/osal/tasks/IScheduledTask.h"
#include "sputteros/utils/logging/LightweightStringBuilder.h"
#include "sputteros/utils/logging/TelemetryLogger.h"
#include <cstdint>

namespace Heartbeat
{

/**
 * @brief Emits "Pulse" to telemetry at a fixed interval.
 *
 * Elapsed time is tracked via unsigned subtraction on `SputterMicros`
 * values, giving correct rollover behaviour that matches bare-metal
 * timer arithmetic.
 */
class PulseTask : public SputterOS::IScheduledTask
{
  public:
    /** @brief Interval between successive "Pulse" messages (milliseconds). */
    static constexpr uint32_t kPulseIntervalMs = 500;

    /** @brief Interval between successive "Pulse" messages (microseconds). */
    static constexpr SputterOS::SputterMicros kPulseIntervalUs =
        static_cast<SputterOS::SputterMicros>(kPulseIntervalMs) * 1000;

    /** @brief The task's activation period in microseconds. */
    SputterOS::SputterMicros periodUs() const override { return kPulseIntervalUs; }

    /**
     * @brief Construct a PulseTask that writes to the given TelemetryLogger.
     * @param telemetry: Shared logger — lifetime must exceed this task.
     */
    explicit PulseTask(SputterOS::TelemetryLogger &telemetry) : m_telemetry(telemetry), m_lastPulse{0}, m_pulseCount(0)
    {
    }

    /**
     * @brief Reset the last-pulse timestamp and pulse counter.
     */
    void init() override
    {
        m_lastPulse  = 0;
        m_pulseCount = 0;
    }

    /**
     * @brief Emit a formatted pulse message if the interval has elapsed.
     *
     * @param systemTimeMicros: Monotonic system time supplied by the scheduler.
     */
    void tick(SputterOS::SputterMicros systemTimeMicros) override
    {
        const SputterOS::SputterMicros elapsed = systemTimeMicros - m_lastPulse;
        if (elapsed >= kPulseIntervalUs)
        {
            m_lastPulse = systemTimeMicros;
            ++m_pulseCount;

            m_builder.clear();
            m_builder.append("Pulse #").append(static_cast<uint32_t>(m_pulseCount));

            m_telemetry.log(SputterOS::TelemetryLogger::TaskID::SYSTEM, m_builder.c_str(),
                            SputterOS::TelemetryLogger::Verbosity::STATUS, systemTimeMicros);
        }
    }

  private:
    SputterOS::TelemetryLogger         &m_telemetry;  /**< @brief Shared telemetry buffer. */
    SputterOS::LightweightStringBuilder m_builder;    /**< @brief Reusable message formatter. */
    SputterOS::SputterMicros            m_lastPulse;  /**< @brief Timestamp of the last Pulse emit (\u00b5s). */
    uint32_t                            m_pulseCount; /**< @brief Running count of emitted pulses. */
};

} // namespace Heartbeat

#endif // HEARTBEAT_SRC_PULSETASK_H
