#ifndef SENSORPOLL_INCLUDE_ADCPOLLTASK_H
#define SENSORPOLL_INCLUDE_ADCPOLLTASK_H

/**
 * @file AdcPollTask.h
 * @brief Scheduled task demonstrating the IO_PENDING pattern.
 *
 * Runs at 20 Hz (50 ms period).  Each tick either starts a new ADC
 * conversion or polls an in-flight one.  When the conversion is not
 * yet ready, the task returns early and signals IO_PENDING via
 * `isIoPending()`.  The Cruncher marks the slot as `IO_PENDING`
 * rather than `IDLE`, and re-dispatches on the next activation.
 *
 * If the simulated ADC hangs (exceeds `kMaxIoRetries` consecutive
 * polls), the task logs a fault and resets the conversion cycle.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/11/2026
 */

#include "SensorPollHAL.h"
#include "sputteros/osal/SputterTime.h"
#include "sputteros/osal/tasks/IScheduledTask.h"
#include "sputteros/utils/logging/LightweightStringBuilder.h"
#include "sputteros/utils/logging/TelemetryLogger.h"
#include <cstdint>

namespace SensorPoll
{

/**
 * @brief Non-blocking ADC poller — demonstrates IO_PENDING scheduling.
 *
 * State machine:
 * @code
 *   IDLE → startConversion() → WAITING → isReady? → readResult → IDLE
 *                                  ↓ no
 *                           yield (IO_PENDING) → re-dispatch next tick
 * @endcode
 */
class AdcPollTask : public SputterOS::IScheduledTask
{
  public:
    /** @brief Polling period (50 ms = 20 Hz). */
    static constexpr SputterOS::SputterMicros kPeriodUs = 50'000;

    /** @brief Max consecutive polls before declaring IO timeout. */
    static constexpr uint32_t kMaxIoRetries = 10;

    SputterOS::SputterMicros periodUs() const override { return kPeriodUs; }

    /**
     * @brief Signal the Cruncher that this task is waiting for IO.
     * @return true if an ADC conversion is in flight.
     */
    bool isIoPending() const override { return m_waitingForAdc; }

    /**
     * @brief Construct an AdcPollTask.
     * @param telemetry Shared telemetry logger.
     * @param adc       Simulated ADC device — lifetime must exceed this task.
     */
    AdcPollTask(SputterOS::TelemetryLogger &telemetry, SimulatedADC &adc)
        : m_telemetry(telemetry), m_adc(adc), m_waitingForAdc(false), m_ioWaitCount(0), m_readingCount(0),
          m_lastReading(0.0f)
    {
    }

    void init() override
    {
        m_waitingForAdc = false;
        m_ioWaitCount   = 0;
        m_readingCount  = 0;
        m_lastReading   = 0.0f;
        m_lastTick      = 0;
    }

    /**
     * @brief Poll ADC or start new conversion.
     *
     * Rate-limited internally to `kPeriodUs` (except when IO_PENDING,
     * where polling continues each invocation).  Once the Cruncher is
     * implemented, internal gating becomes redundant but harmless.
     *
     * If waiting for a conversion:
     *   - Check if ready → read result, log, reset state.
     *   - Not ready → increment retry counter, yield (IO_PENDING).
     *   - Exceeded retries → log fault, reset state.
     *
     * If idle:
     *   - Start a new conversion, set waiting flag.
     *
     * @param systemTimeMicros Monotonic time from the scheduler.
     */
    void tick(SputterOS::SputterMicros systemTimeMicros) override
    {
        // When not waiting for IO, rate-limit to period.
        if (!m_waitingForAdc)
        {
            if ((systemTimeMicros - m_lastTick) < kPeriodUs)
                return;
            m_lastTick = systemTimeMicros;
        }
        if (m_waitingForAdc)
        {
            if (m_adc.isConversionReady())
            {
                // IO completed — read the result.
                m_lastReading = m_adc.readResult();
                ++m_readingCount;
                m_waitingForAdc = false;
                m_ioWaitCount   = 0;

                // Log the reading.
                m_builder.clear();
                m_builder.append("ADC #").append(m_readingCount).append(" = ").append(m_lastReading).append(" mV");

                m_telemetry.log(SputterOS::TelemetryLogger::TaskID::CONTROL, m_builder.c_str(),
                                SputterOS::TelemetryLogger::Verbosity::STATUS, systemTimeMicros);
            }
            else
            {
                ++m_ioWaitCount;
                if (m_ioWaitCount > kMaxIoRetries)
                {
                    // IO device hung — report fault, do NOT block forever.
                    m_builder.clear();
                    m_builder.append("ADC TIMEOUT after ").append(m_ioWaitCount).append(" polls");

                    m_telemetry.log(SputterOS::TelemetryLogger::TaskID::CONTROL, m_builder.c_str(),
                                    SputterOS::TelemetryLogger::Verbosity::CRITICAL, systemTimeMicros);

                    m_waitingForAdc = false;
                    m_ioWaitCount   = 0;
                }
                // else: yield early — Cruncher marks IO_PENDING
                return;
            }
        }

        // Start a new conversion for the next polling cycle.
        m_adc.startConversion();
        m_waitingForAdc = true;
    }

    /** @brief Total successful ADC readings. */
    uint32_t readingCount() const { return m_readingCount; }

    /** @brief Most recent ADC value (mV). */
    float lastReading() const { return m_lastReading; }

  private:
    SputterOS::TelemetryLogger         &m_telemetry;     /**< @brief Telemetry logger. */
    SimulatedADC                       &m_adc;           /**< @brief Simulated ADC device. */
    SputterOS::LightweightStringBuilder m_builder;       /**< @brief Message formatter. */
    bool                                m_waitingForAdc; /**< @brief Conversion in flight? */
    uint32_t                            m_ioWaitCount;   /**< @brief Consecutive IO polls. */
    uint32_t                            m_readingCount;  /**< @brief Successful readings. */
    float                               m_lastReading;   /**< @brief Last ADC value (mV). */
    SputterOS::SputterMicros            m_lastTick{0};   /**< @brief Last rate-limited tick (µs). */
};

} // namespace SensorPoll

#endif // SENSORPOLL_INCLUDE_ADCPOLLTASK_H
