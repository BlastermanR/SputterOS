#ifndef SPUTTEROS_KERNEL_ISAFETYMONITOR_H
#define SPUTTEROS_KERNEL_ISAFETYMONITOR_H

/**
 * @file ISafetyMonitor.h
 * @brief Generic safety monitor interface for the kernel control loop.
 *
 * The kernel evaluates a collection of `ISafetyMonitor` instances every
 * tick; if any monitor reports a failsafe condition, the kernel forces a
 * safe abort on the user application.
 *
 * The user application wraps domain-specific safety checks (vacuum
 * interlocks, arc detection, cooling flow, etc.) in concrete
 * `ISafetyMonitor` implementations and passes them to `SystemBuilder`.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/8/2026
 */
namespace SputterOS
{

class ISafetyMonitor
{
  public:
    /**
     * @brief Virtual destructor.
     */
    virtual ~ISafetyMonitor() = default;

    /**
     * @brief Evaluate whether the monitored subsystem is safe.
     * @return true if the subsystem is nominal (safe to proceed),
     *         false if a failsafe condition has been detected.
     *
     * @note Must complete in bounded time — called on the hot control path.
     */
    virtual bool isSafe() const = 0;

    /**
     * @brief Return a short human-readable name for diagnostics.
     * @return Null-terminated string identifying this monitor.
     */
    virtual const char *name() const { return "unnamed"; }

    ISafetyMonitor(const ISafetyMonitor &)            = delete;
    ISafetyMonitor &operator=(const ISafetyMonitor &) = delete;

  protected:
    ISafetyMonitor() = default;
};

} // namespace SputterOS

#endif // SPUTTEROS_KERNEL_ISAFETYMONITOR_H
