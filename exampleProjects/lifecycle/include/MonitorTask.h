#ifndef LIFECYCLE_INCLUDE_MONITORTASK_H
#define LIFECYCLE_INCLUDE_MONITORTASK_H

/**
 * @file MonitorTask.h
 * @brief Scheduled task on Core 1 that observes and reports kernel state.
 *
 * Runs at 2 Hz (500 ms period).  Reads `System<Cfg>::kernelState()`
 * and logs the current state name, demonstrating how tasks can
 * observe kernel lifecycle transitions in real time.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/11/2026
 */

#include "LifecycleConfig.h"
#include "sputteros/kernel/System.h"
#include "sputteros/osal/SputterTime.h"
#include "sputteros/osal/tasks/IScheduledTask.h"
#include "sputteros/utils/logging/LightweightStringBuilder.h"
#include "sputteros/utils/logging/TelemetryLogger.h"
#include <cstdint>

namespace Lifecycle
{

/**
 * @brief Core 1 monitor — logs kernel state every 500 ms.
 */
class MonitorTask : public SputterOS::IScheduledTask
{
  public:
    /** @brief Monitoring period (500 ms = 2 Hz). */
    static constexpr SputterOS::SputterMicros kPeriodUs = 500'000;

    SputterOS::SputterMicros periodUs() const override { return kPeriodUs; }

    /**
     * @brief Construct a MonitorTask.
     * @param telemetry Shared telemetry logger.
     */
    explicit MonitorTask(SputterOS::TelemetryLogger &telemetry)
        : m_telemetry(telemetry), m_reportCount(0), m_lastTick(0)
    {
    }

    void init() override
    {
        m_reportCount = 0;
        m_lastTick    = 0;
    }

    /**
     * @brief Query kernel state and log it.
     *
     * Rate-limited internally to `kPeriodUs`.
     *
     * @param systemTimeMicros Monotonic time from the scheduler.
     */
    void tick(SputterOS::SputterMicros systemTimeMicros) override
    {
        if ((systemTimeMicros - m_lastTick) < kPeriodUs) return;
        m_lastTick = systemTimeMicros;
        using Sys = SputterOS::System<LifecycleConfig>;

        ++m_reportCount;

        const auto state = Sys::kernelState();

        m_builder.clear();
        m_builder.append("[Core 1] State report #")
            .append(m_reportCount)
            .append(": ")
            .append(stateName(state));

        m_telemetry.log(SputterOS::TelemetryLogger::TaskID::COMMS,
                        m_builder.c_str(),
                        SputterOS::TelemetryLogger::Verbosity::STATUS,
                        systemTimeMicros);
    }

  private:
    SputterOS::TelemetryLogger         &m_telemetry;  /**< @brief Telemetry logger. */
    SputterOS::LightweightStringBuilder m_builder;     /**< @brief Message formatter. */
    uint32_t                            m_reportCount; /**< @brief Reports emitted. */
    SputterOS::SputterMicros            m_lastTick;    /**< @brief Last rate-limited tick (µs). */

    /**
     * @brief Convert a KernelState to a human-readable name.
     * @param state The kernel state to convert.
     * @return A compile-time string literal.
     */
    static const char *stateName(SputterOS::Kernel::KernelState state)
    {
        switch (state)
        {
        case SputterOS::Kernel::KernelState::UNCONFIGURED:  return "UNCONFIGURED";
        case SputterOS::Kernel::KernelState::CONFIGURED:     return "CONFIGURED";
        case SputterOS::Kernel::KernelState::INITIALIZING:   return "INITIALIZING";
        case SputterOS::Kernel::KernelState::RUNNING:        return "RUNNING";
        case SputterOS::Kernel::KernelState::SUSPENDING:     return "SUSPENDING";
        case SputterOS::Kernel::KernelState::SUSPENDED:      return "SUSPENDED";
        case SputterOS::Kernel::KernelState::ABORTING:       return "ABORTING";
        case SputterOS::Kernel::KernelState::ABORTED:        return "ABORTED";
        case SputterOS::Kernel::KernelState::SHUTTING_DOWN:  return "SHUTTING_DOWN";
        case SputterOS::Kernel::KernelState::SHUTDOWN:       return "SHUTDOWN";
        default:                                             return "UNKNOWN";
        }
    }
};

} // namespace Lifecycle

#endif // LIFECYCLE_INCLUDE_MONITORTASK_H
