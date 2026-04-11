#ifndef SENSORPOLL_HAL_IMPL_SENSORPOLLHAL_H
#define SENSORPOLL_HAL_IMPL_SENSORPOLLHAL_H

/**
 * @file SensorPollHAL.h
 * @brief Stub HAL implementations for the SensorPoll example.
 *
 * Provides the standard stdout stream and always-safe monitor,
 * plus a `SimulatedADC` that emulates a slow ADC conversion
 * to exercise the IO_PENDING pattern.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/11/2026
 */

#include "sputteros/hal/devices/IStream.h"
#include "sputteros/kernel/interfaces/ISafetyMonitor.h"
#include "sputteros/osal/SputterTime.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace SensorPoll
{

// =============================================================================
// StdoutStreamReader
// =============================================================================

/**
 * @brief Stream that sinks input to /dev/null and writes output to stdout.
 */
class StdoutStreamReader : public SputterOS::IStream
{
  public:
    std::size_t available() const override { return 0; }
    std::size_t read(uint8_t * /*buffer*/, std::size_t /*max_len*/) override { return 0; }
    std::size_t write(const uint8_t *data, std::size_t len) override { return std::fwrite(data, 1, len, stdout); }
    bool        isConnected() const override { return true; }
};

// =============================================================================
// AlwaysSafeSafetyMonitor
// =============================================================================

/**
 * @brief Safety monitor that is permanently safe.
 */
class AlwaysSafeSafetyMonitor : public SputterOS::ISafetyMonitor
{
  public:
    bool        isSafe() const override { return true; }
    const char *name() const override { return "AlwaysSafe"; }
};

// =============================================================================
// SimulatedADC
// =============================================================================

/**
 * @brief Simulated ADC with a configurable conversion delay.
 *
 * Models a real ADC that requires time between `startConversion()`
 * and `readResult()`.  The caller must poll `isConversionReady()`
 * until the conversion completes.  This exercises the IO_PENDING
 * pattern: a scheduled task starts the conversion, yields, and
 * checks readiness on subsequent ticks.
 *
 * The simulated value cycles through a sawtooth pattern (0–999 mV).
 */
class SimulatedADC
{
  public:
    /**
     * @brief Construct a SimulatedADC.
     * @param conversionDelayUs How long a conversion takes (µs).
     * @param getTime           Platform time function for simulating delay.
     */
    explicit SimulatedADC(SputterOS::SputterMicros conversionDelayUs, SputterOS::MicrosecondSource getTime)
        : m_conversionDelayUs(conversionDelayUs), m_getTime(getTime), m_conversionStartTime(0),
          m_conversionInFlight(false), m_nextValue(0)
    {
    }

    /**
     * @brief Start an asynchronous ADC conversion.
     *
     * After calling this, poll `isConversionReady()` to check
     * when the result is available.
     */
    void startConversion()
    {
        m_conversionStartTime = m_getTime();
        m_conversionInFlight  = true;
    }

    /**
     * @brief Check whether the in-flight conversion has completed.
     * @return true if the conversion is done and `readResult()` is valid.
     */
    bool isConversionReady() const
    {
        if (!m_conversionInFlight)
            return false;
        return (m_getTime() - m_conversionStartTime) >= m_conversionDelayUs;
    }

    /**
     * @brief Read the completed conversion result.
     *
     * Only valid after `isConversionReady()` returns true.
     * Returns a sawtooth value cycling 0–999 (simulating millivolts).
     *
     * @return Simulated ADC reading in millivolts.
     */
    float readResult()
    {
        m_conversionInFlight = false;
        const float val      = static_cast<float>(m_nextValue);
        m_nextValue          = (m_nextValue + 37) % 1000; // Sawtooth, step 37
        return val;
    }

    /** @brief Whether a conversion is currently in flight. */
    bool isActive() const { return m_conversionInFlight; }

  private:
    SputterOS::SputterMicros     m_conversionDelayUs;   /**< @brief Simulated conversion time. */
    SputterOS::MicrosecondSource m_getTime;             /**< @brief Platform time source. */
    SputterOS::SputterMicros     m_conversionStartTime; /**< @brief When conversion started. */
    bool                         m_conversionInFlight;  /**< @brief Conversion in progress? */
    uint32_t                     m_nextValue;           /**< @brief Next reading to return. */
};

} // namespace SensorPoll

#endif // SENSORPOLL_HAL_IMPL_SENSORPOLLHAL_H
