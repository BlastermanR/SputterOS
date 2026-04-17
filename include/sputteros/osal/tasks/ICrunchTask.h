#ifndef SPUTTEROS_OSAL_ICRUNCHTASK_H
#define SPUTTEROS_OSAL_ICRUNCHTASK_H

/**
 * @file ICrunchTask.h
 * @brief Interface for exclusive-core, blocking-tolerant tight-loop tasks.
 *
 * Tasks that inherit `ICrunchTask` run as the sole task on a dedicated
 * core in `CRUNCH` dispatch mode. Unlike scheduled or background tasks,
 * a crunch task may perform bounded blocking operations (e.g. SPI
 * transactions) within its `crunch()` method. The `CrunchDispatcher`
 * manages the tight loop, providing watchdog kicks, overrun detection,
 * and safety-abort forwarding.
 *
 * A crunch task declares its intended iteration period via
 * `crunchPeriodUs()` and its worst-case single-iteration time via
 * `maxIterationUs()`. The dispatcher uses these to scale watchdog
 * timeouts and detect overruns.
 *
 * ### Registration
 *
 * @code
 * builder.core(1).setCrunchTask(&myServoTask);
 * @endcode
 *
 * ### Build-Time Constraints
 *
 * - A `CRUNCH` core may have exactly one `ICrunchTask` and zero other tasks.
 * - `ICrunchTask` cannot be registered on Core 0 (reserved for FLAT_LOOP).
 * - `kCoreCount >= 2` when any core is in `CRUNCH` mode.
 * - `crunchPeriodUs() >= CfgMinSchedulePeriodUs`.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/13/2026
 */

#include "sputteros/osal/SputterTime.h"
#include "sputteros/osal/tasks/ITask.h"

namespace SputterOS
{

/**
 * @brief Base class for exclusive-core, blocking-tolerant tight-loop tasks.
 *
 * Non-copyable. Subclasses must implement `crunch()`, `crunchPeriodUs()`,
 * `maxIterationUs()`, and the `ITask` interface (`init()`, `tick()`).
 * The `tick()` method is unused in CRUNCH mode — `crunch()` is the
 * primary entry point called by `CrunchDispatcher`.
 */
class ICrunchTask : public ITask
{
  public:
    /** @brief Virtual destructor. */
    virtual ~ICrunchTask() = default;

    ICrunchTask(const ICrunchTask &)            = delete;
    ICrunchTask &operator=(const ICrunchTask &) = delete;

    // -----------------------------------------------------------------
    // Crunch-Specific Interface
    // -----------------------------------------------------------------

    /**
     * @brief Single iteration of the tight loop.
     *
     * Called by `CrunchDispatcher` every iteration. May perform bounded
     * blocking operations (e.g. SPI transactions, short busy-waits).
     * Must complete within `maxIterationUs()` to avoid overrun detection.
     *
     * @param now Current monotonic system time in microseconds.
     */
    virtual void crunch(SputterMicros now) = 0;

    /**
     * @brief Declared iteration period in microseconds.
     *
     * Used by `CrunchDispatcher` for watchdog timeout scaling. Must
     * return a non-zero value >= `CfgMinSchedulePeriodUs`.
     *
     * @return Period in microseconds.
     */
    virtual SputterMicros crunchPeriodUs() const = 0;

    /**
     * @brief Declared worst-case single-iteration time in microseconds.
     *
     * Used by `CrunchDispatcher` for overrun detection. If the actual
     * iteration exceeds this value, an overrun is logged.
     *
     * @return WCET in microseconds.
     */
    virtual SputterMicros maxIterationUs() const = 0;

    /**
     * @brief Emergency shutdown callback — called when safety abort fires.
     *
     * Override to disable actuators, safe motors, or release resources.
     * The default implementation is a no-op.
     */
    virtual void onCrunchAbort() {}

    // -----------------------------------------------------------------
    // Scheduling Type Markers (final overrides)
    // -----------------------------------------------------------------

    /**
     * @brief Crunch task type marker — always true.
     * @return true.
     */
    bool isCrunchTask() const final { return true; }

    /**
     * @brief Scheduling type marker — always false for crunch tasks.
     * @return false.
     */
    bool isScheduled() const final { return false; }

    /**
     * @brief Scheduling type marker — always false for crunch tasks.
     * @return false.
     */
    bool isBackground() const final { return false; }

  protected:
    /** @brief Default constructor (protected — instantiate subclasses only). */
    ICrunchTask() = default;
};

} // namespace SputterOS

#endif // SPUTTEROS_OSAL_ICRUNCHTASK_H
